#ifndef __BICG_H__
#define __BICG_H__

#include "bind_defs.h"

// data type to do computation with
#define DTYPE float

// NO_VEC
// MANYCORE_PREFETCH
// PER_CORE_SIMD
// [VECTOR_LEN=4,16 + any combo of PER_CORE_SIMD, LONGLINES] 

#ifdef VECTOR_LEN
#define USE_VEC
#endif
#ifdef MANYCORE_PREFETCH
#define VECTOR_LEN (1)
#endif

// prefetch sizing
#if defined(USE_VEC) || defined(MANYCORE_PREFETCH)

// number of frames to get ahead
#ifndef INIT_FRAMES
#define INIT_FRAMES 2
#endif

// lenght of a prefetch
#define Q_PREFETCH_LEN 16
#define S_PREFETCH_LEN VECTOR_LEN

// number of 32bits words per frame
#define FRAME_SIZE (2*Q_PREFETCH_LEN)
// number of frames
#define NUM_FRAMES 12
// scratchpad offset after prefetch frames
#define POST_FRAME_WORD (FRAME_SIZE * NUM_FRAMES)
#define INIT_SPM_OFFSET (INIT_FRAMES * FRAME_SIZE)


#ifdef MANYCORE_PREFETCH
  #define VERTICAL_FETCH_TYPE (TO_SELF)
  #define HORIZONTAL_FETCH_TYPE (TO_SELF)
#else
  #define VERTICAL_FETCH_TYPE (TO_ONE_CORE)
  #define HORIZONTAL_FETCH_TYPE (TO_ALL_CORES)
#endif

// prefetch a and r
inline void prefetch_s_frame(DTYPE *a, DTYPE *r, int i, int j, int *sp, int NY) {
  // don't think need prefetch_R here?
  for (int u = 0; u < Q_PREFETCH_LEN; u++) {
    int iun = i + u;
    VPREFETCH_L(*sp + u, &a[iun * NY + j], 0, 1, HORIZONTAL_FETCH_TYPE);
  }
  // VPREFETCH_R(*sp, &a[i * NY + j], 0, S_PREFETCH_LEN, HORIZONTAL);
  // printf("horiz pf i %d j %d idx %d sp %d val %f %f %f %f\n", i, j, i * NY + j, *sp, a[i* NY + j], a[i*NY+j+1], a[i*NY+j+2], a[i*NY+j+3]);
  // *sp = *sp + Q_PREFETCH_LEN;

  // TODO potentially some reuse here b/c fetch the same thing for everyone
  for (int core = 0; core < VECTOR_LEN; core++) {
    VPREFETCH_L(*sp + Q_PREFETCH_LEN, &r[i], core, Q_PREFETCH_LEN, VERTICAL_FETCH_TYPE);
    // VPREFETCH_R(*sp, &r[i], core, Q_PREFETCH_LEN, VERTICAL);
  }

  #ifndef MANYCORE_PREFETCH
  *sp = *sp + FRAME_SIZE;
  if (*sp == POST_FRAME_WORD) *sp = 0;
  #endif
}

// prefetch a and p
inline void prefetch_q_frame(DTYPE *a, DTYPE *p, int i, int j, int *sp, int NY) {
  // don't think need prefetch R here?
  for (int core = 0; core < VECTOR_LEN; core++) {
    int icore = i + core;
    VPREFETCH_LR(*sp, &a[icore * NY + j], core, Q_PREFETCH_LEN, VERTICAL_FETCH_TYPE);
    // VPREFETCH_R(*sp, &a[icore * NY + j], core, Q_PREFETCH_LEN, VERTICAL);
  }

  // *sp = *sp + Q_PREFETCH_LEN;

  // TODO potentially some reuse here b/c fetch the same thing for everyone
  for (int core = 0; core < VECTOR_LEN; core++) {
    VPREFETCH_L(*sp + Q_PREFETCH_LEN, &p[j], core, Q_PREFETCH_LEN, VERTICAL_FETCH_TYPE);
    // VPREFETCH_R(*sp, &p[j], core, Q_PREFETCH_LEN, VERTICAL);
  }

  #ifndef MANYCORE_PREFETCH
  *sp = *sp + FRAME_SIZE;
  if (*sp == POST_FRAME_WORD) *sp = 0;
  #endif
}

#endif

// pthread argument for the kernel
typedef struct Kern_Args {
  DTYPE *a, *r, *p, *s, *q;
  int NX, NY;
  int tid_x, tid_y;
  int dim_x, dim_y;
} Kern_Args;

// helper to pack args
Kern_Args *construct_args(
    DTYPE *a, DTYPE *r, DTYPE *p,
    DTYPE *s, DTYPE *q, 
    int NX, int NY,
    int tid_x, int tid_y, int dim_x, int dim_y
  );

// pthread call
void *pthread_kernel(void *args);

// kernel
void kernel(
    DTYPE *a, DTYPE *r, DTYPE *p,
    DTYPE *s, DTYPE *q, 
    int NX, int NY,
    int tid_x, int tid_y, int dim_x, int dim_y
  );

#endif
