#ifndef __SYR2K_H__
#define __SYR2K_H__

#include "bind_defs.h"

// data type to do computation with
#define DTYPE float

/* Declared constant values for alpha and beta specfically for SYRK bench */
#define alpha 12435
#define beta 4546

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
// dedicate half of scratchpad to frames
#define POST_FRAME_WORD 512

// number of frames to get ahead
#ifndef INIT_FRAMES
#define INIT_FRAMES 2
#endif

// prefetch config for inner kernel
#ifdef LONGLINES
  #define INNER_PREFETCH_LEN (16)
  #if INNER_PREFETCH_LEN > (CACHE_LINE_SIZE / 4 / VECTOR_LEN)
  #error PREFETCH LEN TOO LARGE!
  #endif
  #define J_STRIDE (1)
  #define K_STRIDE (INNER_PREFETCH_LEN * VECTOR_LEN)
#else
  #define INNER_PREFETCH_LEN (8)
  #define J_STRIDE (VECTOR_LEN)
  #define K_STRIDE (INNER_PREFETCH_LEN)
#endif
#define INNER_FRAME_SIZE (4*INNER_PREFETCH_LEN)
#define NUM_FRAMES (POST_FRAME_WORD / INNER_FRAME_SIZE)

#define INIT_OFFSET (INIT_FRAMES * K_STRIDE)

// frame size to get the c to accumulate on
#define OUTER_PREFETCH_LEN INNER_PREFETCH_LEN
#define OUTER_FRAME_SIZE OUTER_PREFETCH_LEN

#if VECTOR_LEN == 4
#define NUM_GROUPS_PER_PIPE (3)
#else
#define NUM_GROUPS_PER_PIPE (1)
#endif

#define ACCUM_GRANULARITY (8)
#define MAILER_OFFSET (NUM_GROUPS_PER_PIPE)
#define SUB_FRAME_SIZE (MAILER_OFFSET + VECTOR_LEN * NUM_GROUPS_PER_PIPE)
#define MAILER_FRAME_SIZE (SUB_FRAME_SIZE * ACCUM_GRANULARITY)
#define MAILER_NUM_FRAMES (POST_FRAME_WORD / MAILER_FRAME_SIZE)
#define MAILER_POST_FRAME_WORD (MAILER_FRAME_SIZE * MAILER_NUM_FRAMES)

// needs to be maxed at number of frame counters
#if MAILER_NUM_FRAMES < 5
#define FRAMES_TO_SYNC_AFTER (MAILER_NUM_FRAMES)
#else
#define FRAMES_TO_SYNC_AFTER (5)
#endif

// how much vector core will write
#define PER_CORE_MAILER_FRAME (VECTOR_LEN)
// includes also if any base value to the sum
#define PER_CORE_FULL_MAILER_FRAME (PER_CORE_MAILER_FRAME + 1)

#ifdef MANYCORE_PREFETCH
  #define VERTICAL_FETCH_TYPE (TO_SELF)
#else
  #define VERTICAL_FETCH_TYPE (TO_ONE_CORE)
#endif

// prefetch c
// pad out to the frame size (1->2 currently)
// maybe don't have to prefetch this
inline void prefetch_outer_frame(DTYPE *c, int i, int j, int *sp, int N) {
  for (int core = 0; core < VECTOR_LEN; core++) {
    VPREFETCH_LR(*sp + INNER_PREFETCH_LEN * 0, &c[i * N + j + core], core, OUTER_PREFETCH_LEN, VERTICAL_FETCH_TYPE);

    // TODO can optmizie, don't need padding anymore with updated FRAME_START
    // pad out
    // VPREFETCH_LR(*sp + INNER_PREFETCH_LEN * 1, &c[i * N + j + core], core, OUTER_PREFETCH_LEN, TO_ONE_CORE);
    // VPREFETCH_LR(*sp + INNER_PREFETCH_LEN * 2, &c[i * N + j + core], core, OUTER_PREFETCH_LEN, TO_ONE_CORE);
    // VPREFETCH_LR(*sp + INNER_PREFETCH_LEN * 3, &c[i * N + j + core], core, OUTER_PREFETCH_LEN, TO_ONE_CORE);
  }

  *sp = *sp + INNER_FRAME_SIZE;
  if (*sp == POST_FRAME_WORD) *sp = 0;
}

// prefetch a
inline void prefetch_inner_frame(DTYPE *a, DTYPE *b, int i, int j, int k, int *sp, int M) {
  #ifdef LONGLINES
  VPREFETCH_L(*sp + INNER_PREFETCH_LEN * 0, 
    &a[i * M + k], 0, INNER_PREFETCH_LEN, TO_ALL_CORES);
  VPREFETCH_L(*sp + INNER_PREFETCH_LEN * 1, 
    &a[j * M + k], 0, INNER_PREFETCH_LEN, TO_ALL_CORES);
  VPREFETCH_L(*sp + INNER_PREFETCH_LEN * 2, 
    &b[i * M + k], 0, INNER_PREFETCH_LEN, TO_ALL_CORES);
  VPREFETCH_L(*sp + INNER_PREFETCH_LEN * 3, 
    &b[j * M + k], 0, INNER_PREFETCH_LEN, TO_ALL_CORES);
  #else
  for (int core = 0; core < VECTOR_LEN; core++) {
    // TODO redundant
    VPREFETCH_L(*sp + INNER_PREFETCH_LEN * 0, &a[i * M + k], core, INNER_PREFETCH_LEN, VERTICAL_FETCH_TYPE);

    // TODO this can be horizontal?
    VPREFETCH_L(*sp + INNER_PREFETCH_LEN * 1, &a[(j + core) * M + k], core, INNER_PREFETCH_LEN, VERTICAL_FETCH_TYPE);

    // fetch b's in the same way
    VPREFETCH_L(*sp + INNER_PREFETCH_LEN * 2, &b[i * M + k], core, INNER_PREFETCH_LEN, VERTICAL_FETCH_TYPE);
    VPREFETCH_L(*sp + INNER_PREFETCH_LEN * 3, &b[(j + core) * M + k], core, INNER_PREFETCH_LEN, VERTICAL_FETCH_TYPE);

  }
  #endif

  #ifndef MANYCORE_PREFETCH
  *sp = *sp + INNER_FRAME_SIZE;
  if (*sp == POST_FRAME_WORD) *sp = 0;
  #endif
}


#endif

// grid dim xy assuming always a square
#if _N_SPS==16
#define GRID_XDIM 4
#define GRID_YDIM 4
#elif _N_SPS==64
#define GRID_XDIM 8
#define GRID_YDIM 8
#endif

// pthread argument for the kernel
typedef struct Kern_Args {
  DTYPE *a, *b, *c;
  int N, M;
  int tid_x, tid_y;
  int dim_x, dim_y;
} Kern_Args;

// helper to pack args
Kern_Args *construct_args(
    DTYPE *a, DTYPE *b, DTYPE *c,
    int N, int M,
    int tid_x, int tid_y, int dim_x, int dim_y
  );

// pthread call
void *pthread_kernel(void *args);

// kernel
void kernel(
    DTYPE *a, DTYPE *b, DTYPE *c,
    int N, int M,
    int tid_x, int tid_y, int dim_x, int dim_y
  );

#endif
