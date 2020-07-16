#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "pthread_launch.h"
#include "conv2d.h"
#include "spad.h"
#include "bind_defs.h"
#include "util.h"
#include "conv2d_kernel.h"

/*
  3x3 stencil with a single 3x3 filter
  Filter is cached in each spad.
  9 values are prefetched per frame
  Parallelize innermost loops (unrolled) so we can get away with codegen trick

  Reuse strategy - constraint is equal frame sizes and can't do remote stores to fill frames
  Each core gets 3 elements just like no reuse case but none are overlapping
  Its up to each core to do compute for those 3 elements and the two adjacents ones 
  Load x + 0, x + 1, x + 2
  Compute
  x - 1, x + 0, x + 1
         x + 0, x + 1, x + 2
                x + 1, x + 2, x + 3
  With this strategy you are the only core to have to load x + 1
  You have to reorder the data to implement! ( spaced out by strides )
  0 1 2 3 | 4 5 6 7  | 8 9 10 11 || 12 13 14 15
  0 3 6 9 | 1 4 7 10 | 2 5 8  11 || 12 15 18 21 

*/

#ifdef USE_VEC

// #define SCALAR_CORE
// #define VECTOR_CORE

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
  for (int k1 = 0; k1 < FILTER_DIM; k1++) {
    for (int core = 0; core < dim; core++) {
      int aIdx = (r + k1) * ncols + c + core * CORE_STEP;
      // int aIdx = core * 16; // at least for size 4, only use 4/8 caches so less bw
      // printf("mid issue r %d c %d k1 %d core %d, depth %d, aIdx %d\n", r, c, k1, core, LOAD_DEPTH, aIdx);
      VPREFETCH_L(*spadIdx, a + aIdx, core, LOAD_DEPTH, 1);
      VPREFETCH_R(*spadIdx, a + aIdx, core, LOAD_DEPTH, 1);
    }
    (*spadIdx)+=LOAD_DEPTH;
  }

  if (*spadIdx == POST_REGION_WORD) *spadIdx = 0;
}
#else
inline void prefetch_horiz_frame(DTYPE *a, int r, int c, int ncols, int pRatio, int *spadIdx) {
  // prefetch all 9 values required for computation
  // #pragma GCC unroll 3
  for (int k1 = 0; k1 < FILTER_DIM; k1++) {
    // #pragma GCC unroll 3
    for (int k2 = 0; k2 < FILTER_DIM; k2++) {
      int aIdx = (r + k1) * ncols + (c + k2);
      
      #if PREFETCH_LEN != VECTOR_LEN
      for (int p = 0; p < pRatio; p++) {
        VPREFETCH_L(*spadIdx, a + aIdx + p * PREFETCH_LEN, p * PREFETCH_LEN, PREFETCH_LEN, 0);
        VPREFETCH_R(*spadIdx, a + aIdx + p * PREFETCH_LEN, p * PREFETCH_LEN, PREFETCH_LEN, 0);
      }
      #else
      VPREFETCH_L(*spadIdx, a + aIdx, 0, PREFETCH_LEN, 0);
      VPREFETCH_R(*spadIdx, a + aIdx, 0, PREFETCH_LEN, 0);
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

void tril_conv2d(int mask,
    DTYPE *a, DTYPE *b, int start_row, int end_row, int ncols,
    int ptid, int vtid_x, int vtid_y, int vdim_x, int vdim_y, int effCols) {

  #ifdef SCALAR_CORE
  VECTOR_EPOCH(mask);

  int dim = vdim_x * vdim_y;
  int vtid = vtid_x + vtid_y * vdim_x;

  #ifdef REUSE
  int step = dim*FILTER_DIM - (FILTER_DIM - 1);
  #elif defined(VERTICAL_LOADS)
  int step = CORE_STEP*dim;
  #else
  int step = dim;
  #endif

  // TODO better way to do this for arbitrary groups
  #ifdef REUSE
  // calculate prev and next spAddr for reuse
  int *prevSpadAddr = NULL;
  int *nextSpadAddr = NULL;
  if (vtid != 0) {
    if (vtid_x == 0) prevSpadAddr = (int*)getSpAddr(ptid - (GRID_XDIM - (vdim_x - 1)), 0);
    else prevSpadAddr = (int*)getSpAddr(ptid - 1, 0);
  }
  if (vtid != dim - 1) {
    if (vtid_x == vdim_x - 1) nextSpadAddr = (int*)getSpAddr(ptid + (GRID_XDIM - (vdim_x - 1)), 0);
    else nextSpadAddr = (int*)getSpAddr(ptid + 1, 0);
  }
  #endif

  // should be a constant from static analysis of dim
  int pRatio = VECTOR_LEN / PREFETCH_LEN;

  ISSUE_VINST(init_label);

  #endif


  #ifdef VECTOR_CORE
  asm("trillium vissue_delim until_next vector_init");
  
	DEF_WEIGHTS();

  #ifdef REUSE
  int startOffset = vtid * FILTER_DIM - 1;
  #else
  int startOffset = vtid * (step/dim);
  #endif

  DTYPE *bPtr = b + start_row * ncols + startOffset;
  int unmappedColLen = ncols - effCols;


  int sp = 0;
  DTYPE* sp_ptr = (DTYPE*)getSpAddr(ptid, 0);
  #endif



  // TODO fails if put this here... sigh
  // // how much we're actually going to do (ignore edges)
  // // TODO can we use predication instead?
  // int effCols = ncols - (FILTER_DIM - 1);

  #ifdef SCALAR_CORE
  int sp = 0;
  int beginCol = min(INIT_FRAMES * step, effCols);

  for (int r = start_row; r < end_row; r++) {
    // initial warmup
    for (int c = 0; c < beginCol; c+=step) {
      #ifdef VERTICAL_LOADS
      // exhibit temporal reuse within a frame in a cacheline (16) can do 16-2=14 3x1 filters
      // TODO spatial should also do reuse maybe between frames (by putting in temporal storage). 
      // But maybe can't do memory layout restrictions
      prefetch_vert_frame(a, r, c, ncols, dim, &sp);
      #else
      prefetch_horiz_frame(a, r, c, ncols, pRatio, &sp);
      #endif
    }

    // steady state
    for (int c = beginCol; c < effCols; c+=step) {
      #ifdef VERTICAL_LOADS
      prefetch_vert_frame(a, r, c, ncols, dim, &sp);
      #else
      prefetch_horiz_frame(a, r, c, ncols, pRatio, &sp);
      #endif
      ISSUE_VINST(vec_body_label);
    }

    // cooldown
    for (int c = effCols - beginCol; c < effCols; c+=step) {
      ISSUE_VINST(vec_body_label);
    }

    ISSUE_VINST(vec_body_end_label);
  }
  #endif

  #ifdef VECTOR_CORE
  volatile int BH;
  volatile int BHO;
  do {
    do {
    asm("trillium vissue_delim if_begin vec_body");

      FRAME_START();

      #ifdef REUSE

      // note we need to unroll in order to get cPtr indexing to work b/c it goes +1 +1 +3*dim
      // potentially can move routine into a function call?
      // also could access an indirection array that gives and then have counter mod 3
      // or could even have 3 seperate issue block


      int spData0 = spadAddr[spIdx + 0];
      int spData1 = spadAddr[spIdx + 1];
      int spData2 = spadAddr[spIdx + 2];
      int spData3 = spadAddr[spIdx + 3];
      int spData4 = spadAddr[spIdx + 4];
      int spData5 = spadAddr[spIdx + 5];
      int spData6 = spadAddr[spIdx + 6];
      int spData7 = spadAddr[spIdx + 7];
      int spData8 = spadAddr[spIdx + 8];

      // center computation with local values
      // important to put non-predicated first so any shared values between pred blocks
      // are not masked out... really need compiler help on this
      c_ = 0;
      c_ += b0 * spData0;
      c_ += b1 * spData1;
      c_ += b2 * spData2;
      c_ += b3 * spData3;
      c_ += b4 * spData4;
      c_ += b5 * spData5;
      c_ += b6 * spData6;
      c_ += b7 * spData7;
      c_ += b8 * spData8;
      STORE_NOACK(c_, cPtr + 1, 0);

      // if swap following two pred blocks core0 pred works, but then core3 pred doesn't work
      // definetly something wrong with pred...
      // fetch one column from the left to perform leftmost computation
      PRED_NEQ(vtid, 0);
      if (ohjeez) {
      c_ = 0;
      c_ += b0 * prevSpadAddr[spIdx + 2];
      c_ += b1 * spData0;
      c_ += b2 * spData1;
      c_ += b3 * prevSpadAddr[spIdx + 5];
      c_ += b4 * spData3;
      c_ += b5 * spData4;
      c_ += b6 * prevSpadAddr[spIdx + 8];
      c_ += b7 * spData6;
      c_ += b8 * spData7;
      STORE_NOACK(c_, cPtr, 0);
      }
      PRED_EQ(vtid, vtid);

      // fetch one column from the right to perform rightmost computation
      PRED_NEQ(vtid, dim - 1); // last core in group can't do this
      if (ohjeez) { 
      c_ = 0;
      c_ += b0 * spData1;
      c_ += b1 * spData2;
      c_ += b2 * nextSpadAddr[spIdx + 0];
      c_ += b3 * spData4;
      c_ += b4 * spData5;
      c_ += b5 * nextSpadAddr[spIdx + 3];
      c_ += b6 * spData7;
      c_ += b7 * spData8;
      c_ += b8 * nextSpadAddr[spIdx + 6];
      STORE_NOACK(c_, cPtr + 2, 0);
      }
      PRED_EQ(vtid, vtid);

      REMEM(frameSize);

      // 10 results are computed per reuse iteration
      // cPtr+=step;
      cPtr += step;
      colCntr+=step;
      CONVERGENT_IF(colCntr == eff_cols) {
        colCntr = 0;
        cPtr += unmappedColLen;
      }

      #elif defined(VERTICAL_LOADS)
      #pragma GCC unroll(14)
      for (int i = 0; i < CORE_STEP; i++) {
        c_ = 0;
        int baseSpIdx = spIdx + i;
        c_ += b0 * spadAddr[baseSpIdx + 0];
        c_ += b1 * spadAddr[baseSpIdx + 1];
        c_ += b2 * spadAddr[baseSpIdx + 2];
        c_ += b3 * spadAddr[baseSpIdx + LOAD_DEPTH + 0];
        c_ += b4 * spadAddr[baseSpIdx + LOAD_DEPTH + 1];
        c_ += b5 * spadAddr[baseSpIdx + LOAD_DEPTH + 2];
        c_ += b6 * spadAddr[baseSpIdx + 2*LOAD_DEPTH + 0];
        c_ += b7 * spadAddr[baseSpIdx + 2*LOAD_DEPTH + 1];
        c_ += b8 * spadAddr[baseSpIdx + 2*LOAD_DEPTH + 2];
        STORE_NOACK(c_, cPtr + i, 0);
      }
      REMEM(frameSize);

      cPtr += step;
      colCntr+=step;
      CONVERGENT_IF(colCntr == eff_cols) {
        colCntr = 0;
        cPtr += unmappedColLen;
      }
      #else
      DTYPE out = 0.0f;
      out += c11 * sp_ptr[sp + 0];
      out += c21 * sp_ptr[sp + 1];
      out += c31 * sp_ptr[sp + 2];
      out += c12 * sp_ptr[sp + 3];
      out += c22 * sp_ptr[sp + 4];
      out += c32 * sp_ptr[sp + 5];
      out += c13 * sp_ptr[sp + 6];
      out += c23 * sp_ptr[sp + 7];
      out += c33 * sp_ptr[sp + 8];

      END_FRAME();

      STORE_NOACK(out, bPtr, 0);
      bPtr += dim;
      #endif

      sp += REGION_SIZE;
      sp = sp % POST_REGION_WORD;
      asm("trillium vissue_delim end at_jump");
    } while (BH);

    asm("trillium vissue_delim if_begin vec_body_end");
    bPtr += unmappedColLen;
    asm("trillium vissue_delim end at_jump");


  } while (BHO);
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

  return;
}
#endif