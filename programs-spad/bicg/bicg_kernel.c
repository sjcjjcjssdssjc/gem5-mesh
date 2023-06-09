#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#include "pthread_launch.h"
#include "bicg.h"
#include "spad.h"
#include "bind_defs.h"
#include "util.h"

#ifdef PER_CORE_SIMD
#include <riscv_vector.h>
#endif

// #define SCALAR_CORE
// #define VECTOR_CORE

/*-----------------------------------------------------------------------------------
 * Vector versions of the kernels.
 *---------------------------------------------------------------------------------*/

#ifdef USE_VEC
void tril_bicg_s(int mask, DTYPE *a, DTYPE *r, DTYPE *s, int NX, int NY, int ptid, int groupId, int numGroups, int vtid) {

  #ifdef SCALAR_CORE
  VECTOR_EPOCH(mask);

  // chunk over vector gorups
  int start = ((groupId + 0) * NY) / numGroups;
  int end   = ((groupId + 1) * NY) / numGroups;

  // make it a factor of vector group mapping size
  start = roundUp(start, VECTOR_LEN);
  end   = roundUp(end  , VECTOR_LEN);

  // issue header block
  ISSUE_VINST(init_label);

  int startOffset = min(INIT_FRAMES*Q_PREFETCH_LEN, NX);
  #endif

  #ifdef VECTOR_CORE
  asm("trillium vissue_delim until_next vector_init");
  int start = ((groupId + 0) * NY) / numGroups;
  start = roundUp(start, VECTOR_LEN);

  int j = start + vtid;
  DTYPE s_local = 0.0f;
  int sp = 0;
  DTYPE* sp_ptr = (DTYPE*)getSpAddr(ptid, 0);

  #ifdef PER_CORE_SIMD
  vsetvl_e32m1(PER_CORE_SIMD_LEN);
  vfloat32m1_t vzero = vfmv_v_f_f32m1(0.0f);
  vfloat32m1_t accum = vzero;
  #endif  
  #endif

  #ifdef SCALAR_CORE
  int sp = 0;
  
  for (int j = start; j < end; j+=VECTOR_LEN) {
    // do initial prefetching for a small amount
    for (int i = 0; i < startOffset; i+=Q_PREFETCH_LEN) {
      prefetch_s_frame(a, r, i, j, &sp, NY);
    }

    // steady state
    for (int i = startOffset; i < NX; i+=Q_PREFETCH_LEN) {
      prefetch_s_frame(a, r, i, j, &sp, NY);
      ISSUE_VINST(vec_body_label);
    }

    // draining. do the last vissue corresponding to the initial round of prefetch
    for (int i = NX - startOffset; i < NX; i+=Q_PREFETCH_LEN) {
      ISSUE_VINST(vec_body_label);
    }

    ISSUE_VINST(vec_body_end_label);
  }

#endif

#ifdef VECTOR_CORE

  volatile int BH;
  do {
    do {
      asm("trillium vissue_delim if_begin vec_body");
      #ifdef PER_CORE_SIMD
      vsetvl_e32m1(PER_CORE_SIMD_LEN);
      #endif

      FRAME_START(FRAME_SIZE);

      #pragma GCC unroll(16)
      for (int i = 0; i < Q_PREFETCH_LEN; i+=PER_CORE_SIMD_LEN) {
        #ifdef PER_CORE_SIMD
        vfloat32m1_t va = vle32_v_f32m1(&sp_ptr[sp + i]);
        vfloat32m1_t vr = vle32_v_f32m1(&sp_ptr[sp + Q_PREFETCH_LEN + i]);
        vfloat32m1_t vs = vfmul_vv_f32m1(va, vr);
        accum = vfadd_vv_f32m1(accum, vs);
        #else
        s_local += sp_ptr[sp + i] * sp_ptr[sp + Q_PREFETCH_LEN + i];
        #endif
      }
      REMEM(FRAME_SIZE);
      sp+=FRAME_SIZE;
      sp = sp % POST_FRAME_WORD;

      asm("trillium vissue_delim end at_jump");
    } while(BH);

    asm("trillium vissue_delim until_next vec_body_end");

    #ifdef PER_CORE_SIMD
    vsetvl_e32m1(PER_CORE_SIMD_LEN);
    vfloat32m1_t vslocal = vfredsum_vs_f32m1_f32m1(accum, accum, vzero);
    s_local += vfmv_f_s_f32m1_f32(vslocal);
    accum = vzero;
    #endif

    FSTORE_NOACK(s_local, &s[j], 0);
    j+=VECTOR_LEN;
    s_local = 0.0f;

 

  } while(BH);

#endif


  // Clean up on the vector cores.
#ifdef SCALAR_CORE
  ISSUE_VINST(vector_return_label);
#elif defined VECTOR_CORE
  asm("trillium vissue_delim return vector_return");
  return;
#endif

#ifdef SCALAR_CORE
  // devec with unique tag
  DEVEC(devec_0);

  // we are doing lazy store acks, so use this to make sure all stores have commited to memory
  asm volatile("fence\n\t");
  asm("trillium vissue_delim return scalar_return");  // XXX is this real???
  return;
#endif

  // Glue points!
#ifdef SCALAR_CORE
init_label:
  asm("trillium glue_point vector_init");
vec_body_label:
  asm("trillium glue_point vec_body");
vec_body_end_label:
  asm("trillium glue_point vec_body_end");
vector_return_label:
  asm("trillium glue_point vector_return");
#endif
}

void tril_bicg_q(int mask, DTYPE *a, DTYPE *p, DTYPE *q, int NX, int NY, int ptid, int groupId, int numGroups, int vtid) {

  #ifdef SCALAR_CORE
  // goto vector mode
  VECTOR_EPOCH(mask);

  // chunk over vector gorups
  int start = ((groupId + 0) * NX) / numGroups;
  int end   = ((groupId + 1) * NX) / numGroups;

  // make it a factor of vector group mapping size
  start = roundUp(start, VECTOR_LEN);
  end   = roundUp(end  , VECTOR_LEN);
  
  int startOffset = min(INIT_FRAMES*Q_PREFETCH_LEN, NY);

  // issue header block
  ISSUE_VINST(init_label);
  #endif

  #ifdef VECTOR_CORE
  asm("trillium vissue_delim until_next vector_init");
  int start = ((groupId + 0) * NX) / numGroups;
  start = roundUp(start, VECTOR_LEN);

  int i = start + vtid;
  DTYPE q_local = 0.0f;
  int sp = 0;
  DTYPE* sp_ptr = (DTYPE*)getSpAddr(ptid, 0);

  #ifdef PER_CORE_SIMD
  vsetvl_e32m1(PER_CORE_SIMD_LEN);
  vfloat32m1_t vzero = vfmv_v_f_f32m1(0.0f);
  vfloat32m1_t accum = vzero;
  #endif 
  #endif

  // TODO seems like can use c functors to implement this pattern
  // just need to give functions for prefetch and vissue?
  #ifdef SCALAR_CORE
  int sp = 0;

  // this kernel needs to do vertical loads due to access pattern of A
  // currently doing a one wide load. want to increase size, but then also have
  // to unroll vector code

  // TODO merge INIT_FRAMES*Q_PREFEATCH_LEN

  for (int i = start; i < end; i+=VECTOR_LEN) {
    // do initial prefetching for a small amount
    for (int j = 0; j < startOffset; j+=Q_PREFETCH_LEN) {
      prefetch_q_frame(a, p, i, j, &sp, NY);
    }

    // steady state
    for (int j = startOffset; j < NY; j+=Q_PREFETCH_LEN) {
      prefetch_q_frame(a, p, i, j, &sp, NY);
      ISSUE_VINST(vec_body_label);
    }

    // draining. do the last vissue corresponding to the initial round of prefetch
    for (int j = NY - startOffset; j < NY; j+=Q_PREFETCH_LEN) {
      ISSUE_VINST(vec_body_label);
    }

    ISSUE_VINST(vec_body_end_label);
  }

  #endif

#ifdef VECTOR_CORE
  volatile int BH;
  do {
    do {
      asm("trillium vissue_delim if_begin vec_body");
      #ifdef PER_CORE_SIMD
      vsetvl_e32m1(PER_CORE_SIMD_LEN);
      #endif

      FRAME_START(FRAME_SIZE);

      #pragma GCC unroll(16)
      for (int j = 0; j < Q_PREFETCH_LEN; j+=PER_CORE_SIMD_LEN) { 
        #ifdef PER_CORE_SIMD
        vfloat32m1_t va = vle32_v_f32m1(&sp_ptr[sp + j]);
        vfloat32m1_t vp = vle32_v_f32m1(&sp_ptr[sp + Q_PREFETCH_LEN + j]);
        vfloat32m1_t vq = vfmul_vv_f32m1(va, vp);
        accum = vfadd_vv_f32m1(accum, vq);
        #else
        q_local += sp_ptr[sp + j] * sp_ptr[sp + Q_PREFETCH_LEN + j];
        #endif
      }
      REMEM(FRAME_SIZE);
      sp += FRAME_SIZE;
      sp = sp % POST_FRAME_WORD;

      asm("trillium vissue_delim end at_jump");
    } while(BH);

    asm("trillium vissue_delim until_next vec_body_end");

    #ifdef PER_CORE_SIMD
    vsetvl_e32m1(PER_CORE_SIMD_LEN);
    vfloat32m1_t vqlocal = vfredsum_vs_f32m1_f32m1(accum, accum, vzero);
    q_local += vfmv_f_s_f32m1_f32(vqlocal);
    accum = vzero;
    #endif

    FSTORE_NOACK(q_local, &q[i], 0);
    i += VECTOR_LEN;
    q_local = 0.0f;

  } while(BH);
#endif

  // Clean up on the vector cores.
#ifdef SCALAR_CORE
  ISSUE_VINST(vector_return_label);
#elif defined VECTOR_CORE
  asm("trillium vissue_delim return vector_return");
  return;
#endif

#ifdef SCALAR_CORE
  // devec with unique tag
  DEVEC(devec_0);

  // we are doing lazy store acks, so use this to make sure all stores have commited to memory
  asm volatile("fence\n\t");
  asm("trillium vissue_delim return scalar_return");  // XXX is this real???
  return;
#endif

  // Glue points!
#ifdef SCALAR_CORE
init_label:
  asm("trillium glue_point vector_init");
vec_body_label:
  asm("trillium glue_point vec_body");
vec_body_end_label:
  asm("trillium glue_point vec_body_end");
vector_return_label:
  asm("trillium glue_point vector_return");
#endif
}

#endif