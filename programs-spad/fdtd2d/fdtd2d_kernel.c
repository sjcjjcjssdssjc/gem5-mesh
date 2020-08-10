#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#include "pthread_launch.h"
#include "fdtd2d.h"
#include "spad.h"
#include "bind_defs.h"
#include "group_templates.h"
#include "util.h"
#include "fdtd2d_kernel.h"

// #define SCALAR_CORE
// #define VECTOR_CORE

/*-----------------------------------------------------------------------------------
 * Vector versions of the kernels.
 *---------------------------------------------------------------------------------*/

#ifdef USE_VEC

void tril_fdtd_step1(int mask,
  DTYPE *fict, DTYPE *ex, DTYPE *ey, DTYPE *hz, int t, int NX, int NY,
  int ptid, int groupId, int numGroups, int vtid) {

  #ifdef SCALAR_CORE
  VECTOR_EPOCH(mask);

  int start = ((groupId + 0) * NX) / numGroups;
  int end   = ((groupId + 1) * NX) / numGroups;

  ISSUE_VINST(init_label);
  #endif

  #ifdef VECTOR_CORE
  asm("trillium vissue_delim until_next vector_init");
  int start = ((groupId + 0) * NX) / numGroups;

  int unmappedLen = 0;
  int idx = start * NY + vtid - unmappedLen;

  int sp = 0;
  DTYPE *sp_ptr = (DTYPE*)getSpAddr(ptid, 0);
  #endif


  #ifdef SCALAR_CORE
  int sp = 0;
  int init_len = min(INIT_FRAMES*VECTOR_LEN, NY);

  for (int i = start; i < end; i++) {

    ISSUE_VINST(vec_body_init_i0_label);
    if (i != 0)
      ISSUE_VINST(vec_body_init_in0_label);

    // warmup
    for (int j = 0; j < init_len; j+=VECTOR_LEN) {
      if (i == 0) {
        prefetch_step1_frame_i0(fict, t, &sp);
      }
      else {
        prefetch_step1_frame_in0(ey, hz, i, j, NY, &sp);
      }
    }

    // steady-state
    for (int j = init_len; j < NY; j+=VECTOR_LEN) {
      if (i == 0) {
        prefetch_step1_frame_i0(fict, t, &sp);
        ISSUE_VINST(vec_body_i0_label);
      }
      else {
        prefetch_step1_frame_in0(ey, hz, i, j, NY, &sp);
        ISSUE_VINST(vec_body_in0_label);
      }
    }

    // coolodown
    for (int j = NY - init_len; j < NY; j+=VECTOR_LEN) {
      if (i == 0) {
        ISSUE_VINST(vec_body_i0_label);
      }
      else {
        ISSUE_VINST(vec_body_in0_label);
      }
    }
  }
  
  #endif

  // TODO do first row in scalar and no vector core. maybe want another kernel?

  #ifdef VECTOR_CORE
  volatile int BH;
  volatile int BHO;

  do {

    asm("trillium vissue_delim until_next vec_body_init_i0");
    idx += unmappedLen;

    // i == 0
    do {
      asm("trillium vissue_delim if_begin vec_body_i0");
      START_FRAME();
      DTYPE out = sp_ptr[sp + 0];
      END_FRAME();
      STORE_NOACK(out, ey + idx, 0);
      idx += VECTOR_LEN;
      sp += STEP1_REGION_SIZE;
      sp = sp % POST_FRAME_WORD;
      #if VECTOR_LEN==16
      #pragma GCC unroll(3)
      for (int n = 0; n < 3; n++) {
        asm volatile("nop\n\t");
      }
      #endif
      // if (sp == POST_FRAME_WORD) sp = 0;
      asm("trillium vissue_delim end at_jump");

    } while(BH);

    asm("trillium vissue_delim until_next vec_body_init_in0");

    // i != 0
    do {

      asm("trillium vissue_delim if_begin vec_body_in0");
      START_FRAME();
      DTYPE out = sp_ptr[sp + 0] - 0.5f * (sp_ptr[sp + 1] - sp_ptr[sp + 2]);
      END_FRAME();
      STORE_NOACK(out, ey + idx, 0);
      idx += VECTOR_LEN;
      sp += STEP1_REGION_SIZE;
      sp = sp % POST_FRAME_WORD;
      // if (sp == POST_FRAME_WORD) sp = 0;
      asm("trillium vissue_delim end at_jump");

    } while(BH);

  } while(BHO);
  #endif



  // Clean up on the vector cores.
#ifdef SCALAR_CORE
  ISSUE_VINST(vector_return_label);
#elif defined VECTOR_CORE
  asm("trillium vissue_delim return vector_return");
  return;
#endif

#ifdef SCALAR_CORE
  DEVEC(devec_0);
  asm volatile("fence\n\t");
  asm("trillium vissue_delim return scalar_return");
  return;
#endif

  // Glue points!
#ifdef SCALAR_CORE
init_label:
  asm("trillium glue_point vector_init");
vec_body_init_i0_label:
  asm("trillium glue_point vec_body_init_i0");
vec_body_init_in0_label:
  asm("trillium glue_point vec_body_init_in0");
vec_body_i0_label:
  asm("trillium glue_point vec_body_i0");
vec_body_in0_label:
  asm("trillium glue_point vec_body_in0");
// vec_body_end_label:
//   asm("trillium glue_point vec_body_end");
vector_return_label:
  asm("trillium glue_point vector_return");
#endif

}

// weird bounds so need to only do up to eff_NY
void tril_fdtd_step2(int mask,
  DTYPE *ex, DTYPE *ey, DTYPE *hz, int t, int NX, int NY,
  int ptid, int groupId, int numGroups, int vtid) {
   #ifdef SCALAR_CORE
  VECTOR_EPOCH(mask);

  int start = ((groupId + 0) * NX) / numGroups;
  int end   = ((groupId + 1) * NX) / numGroups;

  ISSUE_VINST(init_label);
  #endif

  #ifdef VECTOR_CORE
  asm("trillium vissue_delim until_next vector_init");
  int start = ((groupId + 0) * NX) / numGroups;
  
  int i = start - 1;
  int j = 1 + vtid;

  int sp = 0;
  DTYPE *sp_ptr = (DTYPE*)getSpAddr(ptid, 0);
  #endif


  #ifdef SCALAR_CORE
  int sp = 0;
  int init_len = min(INIT_FRAMES*VECTOR_LEN, NY-1);

  for (int i = start; i < end; i++) {

    ISSUE_VINST(vec_body_init_label);

    // warmup
    for (int j = 1; j < 1 + init_len; j+=VECTOR_LEN) {
      prefetch_step2_frame(ex, hz, i, j, NY, &sp);
    }

    // steady-state
    for (int j = 1 + init_len; j < NY; j+=VECTOR_LEN) {
      prefetch_step2_frame(ex, hz, i, j, NY, &sp);
      ISSUE_VINST(vec_body_label);
    }

    // coolodown
    for (int j = NY - init_len; j < NY; j+=VECTOR_LEN) {
      ISSUE_VINST(vec_body_label);
    }

    ISSUE_VINST(vec_body_end_label);
  }
  
  #endif

  // TODO do first row in scalar and no vector core. maybe want another kernel?

  #ifdef VECTOR_CORE
  volatile int BH;
  volatile int BHO;

  do {

    asm("trillium vissue_delim until_next vec_body_init");
    i++;
    j = 1 + vtid;

    do {

      asm("trillium vissue_delim if_begin vec_body");
      START_FRAME();
      DTYPE out = sp_ptr[sp + 0] - 0.5f * (sp_ptr[sp + 1] - sp_ptr[sp + 2]);
      END_FRAME();
      int idx = i * (NY+1) + j;
      int gt = (j >= NY);
      PRED_EQ(gt, 0);
      STORE_NOACK(out, ex + idx, 0);
      PRED_EQ(gt, gt);
      j += VECTOR_LEN;
      sp += STEP2_REGION_SIZE;
      sp = sp % POST_FRAME_WORD;
      // if (sp == POST_FRAME_WORD) sp = 0;
      asm("trillium vissue_delim end at_jump");

    } while(BH);

      asm("trillium vissue_delim if_begin vec_body_end");
      asm("trillium vissue_delim end at_jump");

  } while(BHO);
  #endif



  // Clean up on the vector cores.
#ifdef SCALAR_CORE
  ISSUE_VINST(vector_return_label);
#elif defined VECTOR_CORE
  asm("trillium vissue_delim return vector_return");
  return;
#endif

#ifdef SCALAR_CORE
  DEVEC(devec_0);
  asm volatile("fence\n\t");
  asm("trillium vissue_delim return scalar_return");
  return;
#endif

  // Glue points!
#ifdef SCALAR_CORE
init_label:
  asm("trillium glue_point vector_init");
vec_body_init_label:
  asm("trillium glue_point vec_body_init");
vec_body_label:
  asm("trillium glue_point vec_body");
vec_body_end_label:
  asm("trillium glue_point vec_body_end");
vector_return_label:
  asm("trillium glue_point vector_return");
#endif
}

void tril_fdtd_step3(int mask,
  DTYPE *ex, DTYPE *ey, DTYPE *hz, int t, int NX, int NY, 
  int ptid, int groupId, int numGroups, int vtid) {
   #ifdef SCALAR_CORE
  VECTOR_EPOCH(mask);

  int start = ((groupId + 0) * NX) / numGroups;
  int end   = ((groupId + 1) * NX) / numGroups;

  ISSUE_VINST(init_label);
  #endif

  #ifdef VECTOR_CORE
  asm("trillium vissue_delim until_next vector_init");
  int start = ((groupId + 0) * NX) / numGroups;
  
  int unmappedLen = 0; // TODO should by NY?
  int idx = start * NY + vtid - unmappedLen;

  int sp = 0;
  DTYPE *sp_ptr = (DTYPE*)getSpAddr(ptid, 0);
  #endif


  #ifdef SCALAR_CORE
  int sp = 0;
  int init_len = min(INIT_FRAMES*VECTOR_LEN, NY);

  for (int i = start; i < end; i++) {

    ISSUE_VINST(vec_body_init_label);

    // warmup
    for (int j = 0; j < init_len; j+=VECTOR_LEN) {
      prefetch_step3_frame(ex, ey, hz, i, j, NY, &sp);
    }

    // steady-state
    for (int j = init_len; j < NY; j+=VECTOR_LEN) {
      prefetch_step3_frame(ex, ey, hz, i, j, NY, &sp);
      ISSUE_VINST(vec_body_label);
    }

    // coolodown
    for (int j = NY - init_len; j < NY; j+=VECTOR_LEN) {
      ISSUE_VINST(vec_body_label);
    }
  }
  
  #endif

  // TODO do first row in scalar and no vector core. maybe want another kernel?

  #ifdef VECTOR_CORE
  volatile int BH;
  volatile int BHO;

  do {

    asm("trillium vissue_delim until_next vec_body_init");
    idx += unmappedLen;

    do {

      asm("trillium vissue_delim if_begin vec_body");
      START_FRAME();
      DTYPE out = sp_ptr[sp + 0] - 0.7f * 
        (sp_ptr[sp + 1] - sp_ptr[sp + 2] + sp_ptr[sp + 3] - sp_ptr[sp + 4]);
      END_FRAME();
      STORE_NOACK(out, hz + idx, 0);
      idx += VECTOR_LEN;
      sp += STEP3_REGION_SIZE;
      sp = sp % POST_FRAME_WORD;
      // if (sp == POST_FRAME_WORD) sp = 0;
      asm("trillium vissue_delim end at_jump");

    } while(BH);

  } while(BHO);
  #endif



  // Clean up on the vector cores.
#ifdef SCALAR_CORE
  ISSUE_VINST(vector_return_label);
#elif defined VECTOR_CORE
  asm("trillium vissue_delim return vector_return");
  return;
#endif

#ifdef SCALAR_CORE
  DEVEC(devec_0);
  asm volatile("fence\n\t");
  asm("trillium vissue_delim return scalar_return");
  return;
#endif

  // Glue points!
#ifdef SCALAR_CORE
init_label:
  asm("trillium glue_point vector_init");
vec_body_init_label:
  asm("trillium glue_point vec_body_init");
vec_body_label:
  asm("trillium glue_point vec_body");
vector_return_label:
  asm("trillium glue_point vector_return");
#endif
}

#endif