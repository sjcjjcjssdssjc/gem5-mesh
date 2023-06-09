#ifndef __MVT_H__
#define __MVT_H__

#include "util.h"

// #define VECTOR_LEN 4
#ifdef VECTOR_LEN
#define _VEC
#endif

// #define MANYCORE_PREFETCH

#ifndef INIT_FRAMES
#define INIT_FRAMES 1
#endif

#define REGION_SIZE 16 //configure using LCM of required frame/region sizes
#define NUM_REGIONS (512 / REGION_SIZE) // (0,512) in this case is the hardware region area 

#define PF_BEGIN(pf_len) \
  for (int j = 0; j < n; j+=pf_len){ 

#define PF_END(num_region) \
  REMEM(); \
  spadRegion = (spadRegion + 1) % num_region;  \
  }

#define PF2(off1,off2,data1, data2,idx1,idx2) \
  off1 = spadRegion * REGION_SIZE; \
  off2 = off1 + REGION_SIZE/2; \
  VPREFETCH_L(off1, data1 + idx1, 0, REGION_SIZE/2,TO_SELF); \
  VPREFETCH_L(off2, data2 + idx2, 0, REGION_SIZE/2,TO_SELF); \
  FRAME_START(REGION_SIZE); \
  _Pragma("GCC unroll(16)") \
  for(int jj=0; jj<REGION_SIZE/2; jj+=PER_CORE_SIMD_LEN)

#define PF1(off1,off2,data1,data2,j,i,n) \
  off1 = spadRegion * REGION_SIZE; \
  off2 = off1 + REGION_SIZE/2; \
  VPREFETCH_L(off1, data1 + j, 0, REGION_SIZE/2,TO_SELF); \
  for(int ii=0; ii<REGION_SIZE/2; ii++){  \
    VPREFETCH_L(off2+ii, data2 + ((j+ii)*n + i), 0, 1 ,TO_SELF); \
  } \
  FRAME_START(REGION_SIZE); \
  _Pragma("GCC unroll(16)") \
  for(int jj=0; jj<REGION_SIZE/2; jj+=PER_CORE_SIMD_LEN)

typedef float DTYPE;

// pthread argument for the kernel
typedef struct Kern_Args
{
  DTYPE *a, *y1, *y2, *x1, *x2;
  int n;
  int tid_x, tid_y;
  int dim_x, dim_y;
} Kern_Args;

// helper to pack vvadd args
Kern_Args *construct_args(
    DTYPE *a, DTYPE *y1, DTYPE *y2, DTYPE *x1, DTYPE *x2, int n,
    int tid_x, int tid_y, int dim_x, int dim_y);

// pthread call
void *pthread_kernel(void *args);

// vvadd kernel
void kernel(
    DTYPE *a, DTYPE *y1, DTYPE *y2, DTYPE *x1, DTYPE *x2, int n,
    int tid_x, int tid_y, int dim_x, int dim_y);

#endif
