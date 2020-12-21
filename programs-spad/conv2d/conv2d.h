#ifndef __CONV2D_H__
#define __CONV2D_H__

#include "bind_defs.h"

// filter size
#define FILTER_DIM 3

// data type to do computation with
#define DTYPE float

#define DEF_WEIGHTS() \
  DTYPE c11, c12, c13, c21, c22, c23, c31, c32, c33; \
	c11 = +0.2;  c21 = +0.5;  c31 = -0.8; \
	c12 = -0.3;  c22 = +0.6;  c32 = -0.9; \
	c13 = +0.4;  c23 = +0.7;  c33 = +0.10

#define CONV_3x3(a11, a21, a31, a12, a22, a32, a13, a23, a33) \
  c11 * a11 + c21 * a21 + c31 * a31 + \
  c12 * a12 + c22 * a22 + c32 * a32 + \
  c13 * a13 + c23 * a23 + c33 * a33


// one of these should be defined to dictate config
// #define NO_VEC 1
// #define MANYCORE_PREFETCH
#define PACKED_SIMD

// #define VEC_4_SIMD 1
// #define VEC_4_SIMD_UNROLL 1
// #define VEC_4_SIMD_VERTICAL 1
// #define VEC_4_REUSE_VERTICAL 1

// #define VEC_16_SIMD 1
// #define VEC_16_SIMD_VERTICAL 1
// #define VEC_16_REUSE_VERTICAL 1

// vvadd_execute config directives
#if !defined(NO_VEC) && !defined(MANYCORE_PREFETCH) && !defined(PACKED_SIMD)
#define USE_VEC 1
#endif

// vector grouping directives
#if defined(VEC_4_SIMD) || defined(VEC_4_SIMD_VERTICAL) || defined(VEC_4_REUSE_VERTICAL) || defined(VEC_4_SIMD_UNROLL)
#define VECTOR_LEN 4
#endif
#if defined(VEC_16_SIMD) || defined(VEC_16_SIMD_VERTICAL) || defined(VEC_16_REUSE_VERTICAL)
#define VECTOR_LEN 16
#endif
#if defined(MANYCORE_PREFETCH)
#define VECTOR_LEN 1
#endif

// grid dim xy assuming always a square
#if _N_SPS==16
#define GRID_XDIM 4
#define GRID_YDIM 4
#elif _N_SPS==64
#define GRID_XDIM 8
#define GRID_YDIM 8
#endif

// kernel settings 
#if defined(VEC_4_REUSE) || defined(VEC_4_REUSE_VERTICAL) || defined(VEC_16_REUSE_VERTICAL)
#define REUSE 1
#endif
#if defined(VEC_4_SIMD_VERTICAL) || defined(VEC_16_SIMD_VERTICAL) || \
  defined(VEC_4_REUSE_VERTICAL) || defined(VEC_16_REUSE_VERTICAL) || defined(MANYCORE_PREFETCH) 
#define VERTICAL_LOADS 1
#endif
#if defined(VEC_4_SIMD_UNROLL)
#define UNROLL 4
#endif

#ifdef PACKED_SIMD
#define CORE_STEP (16 - (FILTER_DIM - 1))
#endif

// prefetch sizings
#if defined(USE_VEC) || defined(MANYCORE_PREFETCH)
// can guarentee power of 2 works
#define POST_REGION_WORD 288

#ifndef INIT_FRAMES
#define INIT_FRAMES 2
#endif

#if defined(REUSE)
#define LOAD_DEPTH 3
#define REGION_SIZE (LOAD_DEPTH*FILTER_DIM)
#define NUM_REGIONS (POST_REGION_WORD / REGION_SIZE)
#elif defined(VERTICAL_LOADS)
#define LOAD_DEPTH 8
#define REGION_SIZE (LOAD_DEPTH*FILTER_DIM)
#define NUM_REGIONS (POST_REGION_WORD / REGION_SIZE)
#elif defined(UNROLL)
#define REGION_SIZE (FILTER_DIM * FILTER_DIM * UNROLL)
#define NUM_REGIONS (POST_REGION_WORD / REGION_SIZE)
#else
#define REGION_SIZE (FILTER_DIM * FILTER_DIM)
#define NUM_REGIONS (POST_REGION_WORD / REGION_SIZE)
#endif

// define prefetch len externally
#ifdef PF
#define PREFETCH_LEN PF
#elif defined(VECTOR_LEN)
// default size is the vlen
#define PREFETCH_LEN VECTOR_LEN
#endif

#ifdef VERTICAL_LOADS
// number of filters done per iteration per core
#ifdef REUSE
#define CORE_STEP LOAD_DEPTH
#else
#define CORE_STEP (LOAD_DEPTH - (FILTER_DIM - 1))
#endif
#endif

#ifdef VERTICAL_LOADS
inline void prefetch_vert_frame(DTYPE *a, int r, int c, int ncols, int dim, int *spadIdx) {
  for (int k1 = -1; k1 <= 1; k1++) {
    for (int core = 0; core < dim; core++) {
      int aIdx = (r + k1) * ncols + c - 1 + core * CORE_STEP;
      // int aIdx = core * 16; // at least for size 4, only use 4/8 caches so less bw
      // printf("mid issue r %d c %d k1 %d core %d, depth %d, aIdx %d\n", r, c, k1, core, LOAD_DEPTH, aIdx);
      VPREFETCH_LR(*spadIdx + (k1+1)*LOAD_DEPTH, a + aIdx, core, LOAD_DEPTH, VERTICAL);
    }
    // (*spadIdx)+=LOAD_DEPTH;
  }

  #ifndef MANYCORE_PREFETCH
  (*spadIdx) += REGION_SIZE;
  if (*spadIdx == POST_REGION_WORD) *spadIdx = 0;
  #endif
}
#else
inline void prefetch_horiz_frame(DTYPE *a, int r, int c, int ncols, int pRatio, int *spadIdx) {
  // prefetch all 9 values required for computation
  // #pragma GCC unroll 3
  for (int k1 = -1; k1 <= 1; k1++) {
    // #pragma GCC unroll 3
    for (int k2 = -1; k2 <= 1; k2++) {
      int aIdx = (r + k1) * ncols + (c + k2);
      
      #if PREFETCH_LEN != VECTOR_LEN
      for (int p = 0; p < pRatio; p++) {
        VPREFETCH_L(*spadIdx, a + aIdx + p * PREFETCH_LEN, p * PREFETCH_LEN, PREFETCH_LEN, HORIZONTAL);
        VPREFETCH_R(*spadIdx, a + aIdx + p * PREFETCH_LEN, p * PREFETCH_LEN, PREFETCH_LEN, HORIZONTAL);
      }
      #else
      VPREFETCH_L(*spadIdx, a + aIdx, 0, PREFETCH_LEN, HORIZONTAL);
      VPREFETCH_R(*spadIdx, a + aIdx, 0, PREFETCH_LEN, HORIZONTAL);
      #endif

      (*spadIdx)++;
      
    }
  }

  // spad is circular buffer so do cheap mod here
  if (*spadIdx == POST_REGION_WORD) {
    *spadIdx = 0;
  }
}
#endif
#endif

// pthread argument for the kernel
typedef struct Kern_Args {
  DTYPE *a, *b;
  int NI, NJ;
  int tid_x, tid_y;
  int dim_x, dim_y;
} Kern_Args;

// helper to pack vvadd args
Kern_Args *construct_args(
    DTYPE *a, DTYPE *b, int NI, int NJ,
    int tid_x, int tid_y, int dim_x, int dim_y
  );

// pthread call
void *pthread_kernel(void *args);

// vvadd kernel
void kernel(
    DTYPE *a, DTYPE *b, int NI, int NJ,
    int tid_x, int tid_y, int dim_x, int dim_y
  );

#endif
