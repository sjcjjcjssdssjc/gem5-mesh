#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#include "pthread_launch.h"
#include "fdtd2d.h"
#include "spad.h"
#include "bind_defs.h"
#include "group_templates.h"
#include "fdtd2d_kernel.h"
#include "util.h"

#if defined(PER_CORE_SIMD)
#include <riscv_vector.h>
#endif

/*
  FDTD-2D
*/

/*-----------------------------------------------------------------------------------
 * Manycore. Using PolyBench GPU parallelization strategy. No scratchpad use
 *---------------------------------------------------------------------------------*/

void fdtd_step1_manycore(DTYPE *fict, DTYPE *ex, DTYPE *ey, DTYPE *hz, int t, int NX, int NY, int tid, int dim) {
  int start = ((tid + 0) * NX) / dim;
  int end   = ((tid + 1) * NX) / dim;


  #if defined(MANYCORE_PREFETCH)
  int sp = 0;
  DTYPE* sp_ptr = (DTYPE*)getSpAddr(tid, 0);
  for (int i = start; i < end; i++) {
    if (i == 0) {
      for (int j = 0; j < NY; j+=STEP1_UNROLL_LEN) {     
        prefetch_step1_frame_i0(fict, t, &sp);
        FRAME_START(STEP1_REGION_SIZE);
        #pragma GCC unroll(16)
        for (int u = 0; u < STEP1_UNROLL_LEN; u++) {
          DTYPE out = sp_ptr[sp + 0];
          FSTORE_NOACK(out, &ey[i * NY + j + u], 0);
        }
        sp += STEP1_REGION_SIZE;
        sp = sp % STEP1_POST_FRAME_WORD;
        END_FRAME();
      }
    }
    else {
      for (int j = 0; j < NY; j+=STEP1_UNROLL_LEN) {   
        prefetch_step1_frame_in0(ey, hz, i, j, NY, &sp);
        #ifdef PER_CORE_SIMD
        vsetvl_e32m1(PER_CORE_SIMD_LEN);
        #endif
        FRAME_START(STEP1_REGION_SIZE);
        #pragma GCC unroll(16)
        for (int u = 0; u < STEP1_UNROLL_LEN; u+=PER_CORE_SIMD_LEN) {
          int u0 = u;
          int u1 = STEP1_UNROLL_LEN+u;
          int u2 = 2*STEP1_UNROLL_LEN+u;
          #ifdef PER_CORE_SIMD
          vfloat32m1_t vhzi   = vle32_v_f32m1(&sp_ptr[sp + u1]);
          vfloat32m1_t vhzim1 = vle32_v_f32m1(&sp_ptr[sp + u2]);
          vfloat32m1_t vey    = vle32_v_f32m1(&sp_ptr[sp + u0]);
          vfloat32m1_t vhzsub = vfsub_vv_f32m1(vhzi, vhzim1);
          vfloat32m1_t out = vfmul_vf_f32m1(vhzsub, -0.5f);
          out = vfadd_vv_f32m1(vey, out);
          int idx = i * NY + j;
          vse32_v_f32m1(&ey[idx + u], out);
          #else
          DTYPE out = sp_ptr[sp + u0] - 0.5f * (sp_ptr[sp + u1] - sp_ptr[sp + u2]);
          FSTORE_NOACK(out, &ey[i * NY + j + u], 0);
          #endif
        }
        sp += STEP1_REGION_SIZE;
        sp = sp % STEP1_POST_FRAME_WORD;
        END_FRAME();
      }
    }
  }
  #elif defined(PER_CORE_SIMD)
  for (int i = start; i < end; i++) {
    int chunk = NY;
    for (size_t l; (l = vsetvl_e32m1(chunk)) > 0; chunk -= l) {
      l = vsetvl_e32m1(chunk);
      int j = NY - chunk;

      vfloat32m1_t out;
      if (i == 0) {
        // l = vsetvl_e32m1(chunk);
        out = vfmv_v_f_f32m1(fict[t]);
      }
      else {
        // l = vsetvl_e32m1(chunk);
        vfloat32m1_t vhzi   = vle32_v_f32m1(&hz[i * NY + j]);
        vfloat32m1_t vhzim1 = vle32_v_f32m1(&hz[(i-1) * NY + j]);
        vfloat32m1_t vey    = vle32_v_f32m1(&ey[i * NY + j]);

        vfloat32m1_t vhzsub = vfsub_vv_f32m1(vhzi, vhzim1);
        out = vfmul_vf_f32m1(vhzsub, -0.5f);
        out = vfadd_vv_f32m1(vey, out);
      }
      vse32_v_f32m1(&ey[i * NY + j], out);
    }
  }
  #else
  for (int i = start; i < end; i++) {
    #pragma GCC unroll(16)
    for (int j = 0; j < NY; j++) {
      DTYPE out;
      if (i == 0) {
        out = fict[t];
      }
      else {
        out = ey[i * NY + j] - 0.5f * (hz[i * NY + j] - hz[(i-1) * NY + j]);
      }
      FSTORE_NOACK(out, &ey[i * NY + j], 0);
    }
  }
  #endif
  asm volatile("fence\n\t");
}

void fdtd_step2_manycore(DTYPE *ex, DTYPE *ey, DTYPE *hz, int t, int NX, int NY, int tid, int dim) {
  int start = ((tid + 0) * NX) / dim;
  int end   = ((tid + 1) * NX) / dim;

  #if defined(MANYCORE_PREFETCH)
  int sp = 0;
  DTYPE* sp_ptr = (DTYPE*)getSpAddr(tid, 0);
  #ifdef PER_CORE_SIMD
  vsetvl_e32m1(PER_CORE_SIMD_LEN);
  vint32m1_t cresendo = vmv_v_x_i32m1(0.0f);
  #pragma GCC unroll(16)
  for (int i = 0; i < PER_CORE_SIMD_LEN; i++) {
    cresendo = vslide1down_vx_i32m1(cresendo, i);
  }
  #endif
  for (int i = start; i < end; i++) {
    for (int j = 1; j < NY; j+=STEP2_UNROLL_LEN) {
      prefetch_step2_frame(ex, hz, i, j, NY, &sp);
      #ifdef PER_CORE_SIMD
      vsetvl_e32m1(PER_CORE_SIMD_LEN);
      #endif
      FRAME_START(STEP2_REGION_SIZE);
      #pragma GCC unroll(16)
      for (int u = 0; u < STEP2_UNROLL_LEN; u+=PER_CORE_SIMD_LEN) {
        int u0 = u;
        int u1 = STEP2_UNROLL_LEN + u;
        #ifdef PER_CORE_SIMD
        vfloat32m1_t vhz1 = vle32_v_f32m1(&sp_ptr[sp + u1]);
        vfloat32m1_t vhz  = vfslide1down_vf_f32m1(vhz1, sp_ptr[sp + u1 + PER_CORE_SIMD_LEN]);
        vfloat32m1_t vex  = vle32_v_f32m1(&sp_ptr[sp + u0]);
        vfloat32m1_t vhzz = vfsub_vv_f32m1(vhz, vhz1);
        vfloat32m1_t vout = vfmul_vf_f32m1(vhzz, -0.5f);
        vout = vfadd_vv_f32m1(vex, vout);
        vbool32_t bmask = vmslt_vx_i32m1_b32(cresendo, NY - (j + u));
        vse32_v_f32m1_m(bmask, &ex[i * (NY+1) + j + u], vout);
        #else
        if (j + u < NY) {
          DTYPE out = sp_ptr[sp + u0] - 
            0.5f * (sp_ptr[sp + u1 + 1] - sp_ptr[sp + u1]);
          FSTORE_NOACK(out, &ex[i * (NY+1) + j + u], 0);
        }
        #endif
      }
      sp += STEP2_REGION_SIZE;
      sp = sp % STEP2_POST_FRAME_WORD;
      END_FRAME();
    }
  }
  #elif defined(PER_CORE_SIMD)
  for (int i = start; i < end; i++) {
    int chunk = NY - 1;
    for (size_t l; (l = vsetvl_e32m1(chunk)) > 0; chunk -= l) {
      l = vsetvl_e32m1(chunk);
      int j = 1 + (NY-1) - chunk;

      // vfloat32m1_t vhz  = vle32_v_f32m1(&hz[i * NY + j]);
      // breaks? // vfloat32m1_t vhz1 = vfslide1up_vf_f32m1(vhz1, hz[i * NY + (j-1)]);
      vfloat32m1_t vhz1 = vle32_v_f32m1(&hz[i * NY + (j-1)]);
      // vfloat32m1_t vhz  = vle32_v_f32m1(&hz[i * NY + j]);
      vfloat32m1_t vhz  = vfslide1down_vf_f32m1(vhz1, hz[i * NY + (j-1+l)]);

      vfloat32m1_t vex  = vle32_v_f32m1(&ex[i * (NY+1) + j]);

      vfloat32m1_t vhzz = vfsub_vv_f32m1(vhz, vhz1);
      vfloat32m1_t vout = vfmul_vf_f32m1(vhzz, -0.5f);
      vout = vfadd_vv_f32m1(vex, vout);

      vse32_v_f32m1(&ex[i * (NY+1) + j], vout);
    }
  }
  #else
  for (int i = start; i < end; i++) {
    #pragma GCC unroll(16)
    for (int j = 1; j < NY; j++) {
      DTYPE out = ex[i * (NY+1) + j] - 0.5f * (hz[i * NY + j] - hz[i * NY + (j-1)]);
      FSTORE_NOACK(out, &ex[i * (NY+1) + j], 0); 
    }
  }
  #endif
  asm volatile("fence\n\t");
}

void fdtd_step3_manycore(DTYPE *ex, DTYPE *ey, DTYPE *hz, int t, int NX, int NY, int tid, int dim) {
  int start = ((tid + 0) * NX) / dim;
  int end   = ((tid + 1) * NX) / dim;

  #if defined(MANYCORE_PREFETCH)
  int sp = 0;
  DTYPE* sp_ptr = (DTYPE*)getSpAddr(tid, 0);
  for (int i = start; i < end; i++) {
    for (int j = 0; j < NY; j+=STEP3_UNROLL_LEN) {
      prefetch_step3_frame(ex, ey, hz, i, j, NY, &sp);
      #ifdef PER_CORE_SIMD
      vsetvl_e32m1(PER_CORE_SIMD_LEN);
      #endif
      FRAME_START(STEP3_REGION_SIZE);
      #pragma GCC unroll(16)
      for (int u = 0; u < STEP3_UNROLL_LEN; u+=PER_CORE_SIMD_LEN) {
        int u0 = u;
        int u1 = STEP3_UNROLL_LEN + u;
        int u2 = 2*STEP3_UNROLL_LEN+1 + u;
        int u3 = 3*STEP3_UNROLL_LEN+1 + u;
        #ifdef PER_CORE_SIMD
        vfloat32m1_t vhz  = vle32_v_f32m1(&sp_ptr[sp + u0]);
        vfloat32m1_t vex  = vle32_v_f32m1(&sp_ptr[sp + u1]);
        vfloat32m1_t vex1 = vfslide1down_vf_f32m1(vex, sp_ptr[sp + u1 + PER_CORE_SIMD_LEN]);
        vfloat32m1_t vey1 = vle32_v_f32m1(&sp_ptr[sp + u2]);
        vfloat32m1_t vey  = vle32_v_f32m1(&sp_ptr[sp + u3]);
        vfloat32m1_t veyy = vfsub_vv_f32m1(vey1, vey);
        vfloat32m1_t vexx = vfsub_vv_f32m1(vex1, vex);
        vfloat32m1_t vout = vfadd_vv_f32m1(vexx, veyy);
        vout = vfmul_vf_f32m1(vout, -0.7f);
        vout = vfadd_vv_f32m1(vhz, vout);
        vse32_v_f32m1(&hz[i * NY + j + u], vout);
        #else
        DTYPE out = sp_ptr[sp + u0] - 0.7f * 
          (sp_ptr[sp + u1+1] - sp_ptr[sp + u1] + sp_ptr[sp + u2] - sp_ptr[sp + u3]);
        FSTORE_NOACK(out, &hz[i * NY + j + u], 0); 
        #endif
      }
      sp += STEP3_REGION_SIZE;
      sp = sp % STEP3_POST_FRAME_WORD;
      END_FRAME();
    }
  }
  #elif defined(PER_CORE_SIMD)
  for (int i = start; i < end; i++) {
    int chunk = NY;
    for (size_t l; (l = vsetvl_e32m1(chunk)) > 0; chunk -= l) {
      l = vsetvl_e32m1(chunk);
      int j = NY - chunk;

      vfloat32m1_t vhz  = vle32_v_f32m1(&hz[i * NY + j]);
      vfloat32m1_t vex  = vle32_v_f32m1(&ex[i * (NY+1) + j]);
      // vfloat32m1_t vex1 = vle32_v_f32m1(&ex[i * (NY+1) + (j+1)]);
      vfloat32m1_t vex1 = vfslide1down_vf_f32m1(vex, ex[i * (NY+1) + (j+l)]); // potentially a little slower than just doing load??
      vfloat32m1_t vey1 = vle32_v_f32m1(&ey[(i + 1) * NY + j]);
      vfloat32m1_t vey  = vle32_v_f32m1(&ey[i * NY + j]);

      vfloat32m1_t veyy = vfsub_vv_f32m1(vey1, vey);
      vfloat32m1_t vexx = vfsub_vv_f32m1(vex1, vex);
      vfloat32m1_t vout = vfadd_vv_f32m1(vexx, veyy);
      vout = vfmul_vf_f32m1(vout, -0.7f);
      vout = vfadd_vv_f32m1(vhz, vout);

      vse32_v_f32m1(&hz[i * NY + j], vout);
    }
  }
  #else
  for (int i = start; i < end; i++) {
    #pragma GCC unroll(16)
    for (int j = 0; j < NY; j++) {
      DTYPE out = hz[i * NY + j] - 0.7f * (ex[i * (NY+1) + (j+1)] - ex[i * (NY+1) + j] + ey[(i + 1) * NY + j] - ey[i * NY + j]);
      FSTORE_NOACK(out, &hz[i * NY + j], 0); 
    }
  }
  #endif

  asm volatile("fence\n\t");
}



void __attribute__((optimize("-fno-inline"))) fdtd(
    DTYPE *fict, DTYPE *ex, DTYPE *ey, DTYPE *hz, int NX, int NY, int tmax,
    int ptid, int vtid, int dim, int groupId, int numGroups,
    int mask, int used
  ) {

    for (int t = 0; t < tmax; t++) {
    #ifndef USE_VEC
      #ifdef MANYCORE_PREFETCH
      SET_PREFETCH_MASK(STEP1_NUM_REGIONS, STEP1_REGION_SIZE, &start_barrier);
      #else
      pthread_barrier_wait(&start_barrier);
      #endif
      fdtd_step1_manycore(fict, ex, ey, hz, t, NX, NY, ptid, dim);
      #ifdef MANYCORE_PREFETCH
      SET_PREFETCH_MASK(STEP2_NUM_REGIONS, STEP2_REGION_SIZE, &start_barrier);
      #else
      pthread_barrier_wait(&start_barrier);
      #endif
      fdtd_step2_manycore(ex, ey, hz, t, NX, NY, ptid, dim);
      #ifdef MANYCORE_PREFETCH
      SET_PREFETCH_MASK(STEP3_NUM_REGIONS, STEP3_REGION_SIZE, &start_barrier);
      #else
      pthread_barrier_wait(&start_barrier);
      #endif
      fdtd_step3_manycore(ex, ey, hz, t, NX, NY, ptid, dim);

    #else
      SET_PREFETCH_MASK(STEP1_NUM_REGIONS, STEP1_REGION_SIZE, &start_barrier);
      // i == 0 on manycore
      if (groupId == 0) {
        fdtd_step1_manycore(fict, ex, ey, hz, t, VECTOR_LEN, NY, vtid, VECTOR_LEN);
      }
      // fdtd_step1_manycore(fict, ex, ey, hz, t, NX, NY, ptid, dim);
      // do rest as vector
      if (used)
        tril_fdtd_step1(mask, fict, ex, ey, hz, t, NX - VECTOR_LEN, NY, ptid, groupId, numGroups, vtid);
      SET_PREFETCH_MASK(STEP2_NUM_REGIONS, STEP2_REGION_SIZE, &start_barrier);
      // fdtd_step2_manycore(ex, ey, hz, t, NX, NY, ptid, dim);
      if (used)
        tril_fdtd_step2(mask, ex, ey, hz, t, NX, NY, ptid, groupId, numGroups, vtid);
      SET_PREFETCH_MASK(STEP3_NUM_REGIONS, STEP3_REGION_SIZE, &start_barrier);
      // fdtd_step3_manycore(ex, ey, hz, t, NX, NY, ptid, dim);
      if (used)
        tril_fdtd_step3(mask, ex, ey, hz, t, NX, NY, ptid, groupId, numGroups, vtid);
    #endif
    }

}

void __attribute__((optimize("-freorder-blocks-algorithm=simple"))) kernel(
    DTYPE *fict, DTYPE *ex, DTYPE *ey, DTYPE *hz, int NX, int NY, int tmax,
    int ptid_x, int ptid_y, int pdim_x, int pdim_y) {
  
  // start recording all stats (all cores)
  if (ptid_x == 0 && ptid_y == 0) {
    stats_on();
  }

  #if VECTOR_LEN==4
  SET_USEFUL_VARIABLES_V4(ptid_x, ptid_y, pdim_x, pdim_y);
  #elif VECTOR_LEN==16
  SET_USEFUL_VARIABLES_V16(ptid_x, ptid_y, pdim_x, pdim_y);
  #else
  SET_USEFUL_VARIABLES_MANYCORE(ptid_x, ptid_y, pdim_x, pdim_y);
  #endif

  // need to set vlen here so doesn't cause squash in vector core on change in value
  #ifdef PER_CORE_SIMD
  vsetvl_e32m1(PER_CORE_SIMD_LEN);
  #endif

  MOVE_STACK_ONTO_SCRATCHPAD();

  // compute fdtd
  fdtd(fict, ex, ey, hz, NX, NY, tmax, ptid, vtid, pdim, unique_id, total_groups, mask, used);

  // restore stack pointer
  RECOVER_DRAM_STACK();

}


// helper functions
Kern_Args *construct_args(DTYPE *fict, DTYPE *ex, DTYPE *ey, DTYPE *hz, int NX, int NY, int tmax,
  int tid_x, int tid_y, int dim_x, int dim_y) {

  Kern_Args *args = (Kern_Args*)malloc(sizeof(Kern_Args));
  
  args->fict = fict;
  args->ex   = ex;
  args->ey   = ey;
  args->hz   = hz;
  args->NX    = NX;
  args->NY    = NY;
  args->tmax  = tmax;
  args->tid_x = tid_x;
  args->tid_y = tid_y;
  args->dim_x = dim_x;
  args->dim_y = dim_y;
  
  return args;
      
}

void *pthread_kernel(void *args) {
  // guarentee one thread goes to each core, by preventing any threads
  // from finishing early
  pthread_barrier_wait(&start_barrier);
  
  // call the spmd kernel
  Kern_Args *a = (Kern_Args*)args;
  
  kernel(a->fict, a->ex, a->ey, a->hz, a->NX, a->NY, a->tmax,
      a->tid_x, a->tid_y, a->dim_x, a->dim_y);

  // reset scratchpad config
  SET_PREFETCH_MASK(0, 0, &start_barrier);

  if (a->tid_x == 0 && a->tid_y == 0) {
    stats_off();
  }

  return NULL;
}
