#include <stdlib.h>
#include <stdio.h>

#include "pthread_launch.h"
#include "gemm.h"
#include "spad.h"
#include "../../common/bind_defs.h"

// #define DRAM
#define _VEC
#define USE_VECTOR_SIMD

#ifdef USE_VECTOR_SIMD
#define VPF
#endif

#ifdef VPF
#define VEC_PFETCH
#define OUT_SPAD
#define IN_SPAD
#elif defined SP
#define OUT_SPAD
#define IN_SPAD
#else
#endif

#define BLK_DIM 4

#if (defined(_VEC) && defined(VEC_PFETCH)) || defined(USE_VECTOR_SIMD)
#define REGION_SIZE BLK_DIM
#define NUM_REGIONS (512 / REGION_SIZE)
#else
#define REGION_SIZE (BLK_DIM * 2)
#define NUM_REGIONS (512 / REGION_SIZE)
#endif

//#define _VEC
//#define UNBLOCKED_INNER
//#define BLOCKED
#define INTERLEAVED

static inline int _idx_(int y, int x, int width)
{
  return (y * width) + x;
}

static inline int get_blk_end(int iter, int bound, int blk_dim)
{
  int iter_next = iter + blk_dim;
  if (iter_next > bound)
  {
    return bound;
  }
  else
  {
    return iter_next;
  }
}

#include "gemm_kernel.h"
/*
// void __attribute__((optimize("-fno-inline"))) 
void __attribute__((optimize("-fno-reorder-blocks")))
gemm_vec_simd(DTYPE *a, DTYPE *b, DTYPE *c, int m, int n, int t, 
    int m_start, int m_end, int n_start, int n_end, int tid, int mask) {


  VECTOR_EPOCH(mask);

  if (tid == 0) {
    stats_on();
  }

  int a_cache_size = REGION_SIZE/2; 
  int b_cache_size = REGION_SIZE/2;
  int c_block_size = BLK_DIM*BLK_DIM; 

  int spadRegion = 0;
  DTYPE *sp_all[4] = {(DTYPE*)getSpAddr(0,0),(DTYPE*)getSpAddr(1,0),(DTYPE*)getSpAddr(2,0),(DTYPE*)getSpAddr(3,0)};
  DTYPE *spAddr =  sp_all[tid];

  int offset_x, offset_y;
  int dim_x = 2; //num cpu in a group in x dim
  int dim_y = 2;
  int total_cores = dim_x*dim_y;

  int tid_x = tid%dim_x;
  int tid_y = tid/dim_y;

  offset_x = BLK_DIM*dim_x;
  offset_y = BLK_DIM*dim_y;

  DTYPE* sp_a = spAddr;
  DTYPE* sp_b = spAddr + a_cache_size;
  
  ISSUE_VINST(fable0);
  //assuming m_start-m_end is divisble by BLK_DIM
  for (int i0 = m_start; i0 < m_end ; i0+=offset_y){
    int i_st = i0 + (tid_y * BLK_DIM);
    for (int j0 = n_start; j0 < n_end; j0+=offset_x) {
      int j_st = j0 + (tid_x * BLK_DIM);
      for (int k = 0; k < t; k++){
        
        sp_a = spAddr + spadRegion*REGION_SIZE + 0;
        sp_b = spAddr + spadRegion*REGION_SIZE + a_cache_size;

        // fetch a in scratchpad
        for (int i = 0; i <(offset_y/total_cores) ; i++) { 
          VPREFETCH(sp_a+ i, a + _idx_(k,i0+(i*total_cores),m), 0);
        }
    
        // fetch b in scratchpad
        for (int j = 0; j <(offset_x/total_cores) ; j++) { 
          VPREFETCH(sp_b+ j, b + _idx_(k,j0+(j*total_cores),m), 0);
        }

        ISSUE_VINST(fable1);
        for (int i = 0; i < BLK_DIM; i++) {
          for (int j = 0; j < BLK_DIM; j++) {
            ISSUE_VINST(fable2);
          }
          ISSUE_VINST(fable3);
        }

        spadRegion = (spadRegion + 1) % NUM_REGIONS;

      }

      //store c in DRAM
      ISSUE_VINST(fable4);
      for (int i = 0; i < BLK_DIM; i++) {
          for (int j = 0; j < BLK_DIM; j++) {
            ISSUE_VINST(fable5);
            
          }
          ISSUE_VINST(fable6);
      }

      ISSUE_VINST(fable7);
    }
    ISSUE_VINST(fable8);
  }

  // devec with unique tag
  DEVEC(devec_0);

  asm volatile("fence\n\t");

  if (tid == 4) {
    stats_off();
  }

  return;


  //vector region

  DTYPE *sp_c;
  int i,j;
  int which_sp_a,sp_offset_a;
  int which_sp_b,sp_offset_b;
  DTYPE a_,b_;

  int i_st, j_st;

  DTYPE *addr_a,*addr_b;

  fable0:
    tid_x = tid%dim_x;
    tid_y = tid/dim_y;
    spadRegion = 0;
    //DTYPE *sp_all[4] = {(DTYPE*)getSpAddr(0,0),(DTYPE*)getSpAddr(1,0),(DTYPE*)getSpAddr(2,0),(DTYPE*)getSpAddr(3,0)};
    sp_c = spAddr + NUM_REGIONS*REGION_SIZE;

    a_cache_size = REGION_SIZE/2; 
    b_cache_size = REGION_SIZE/2;
    c_block_size = BLK_DIM*BLK_DIM; 

    i_st = m_start + (tid_y * BLK_DIM);
    j_st = n_start + (tid_x * BLK_DIM);

  fable1:
    i=0;
    j=0;

  fable2:
    which_sp_a = (tid_y*BLK_DIM + i)%total_cores;
    sp_offset_a = (tid_y*BLK_DIM + i)/total_cores;

    addr_a = sp_all[which_sp_a] + spadRegion*REGION_SIZE + sp_offset_a;

    which_sp_b = (tid_x*BLK_DIM + j)%total_cores;
    sp_offset_b = (tid_x*BLK_DIM + j)/total_cores;
    addr_b = sp_all[which_sp_b] + spadRegion*REGION_SIZE + a_cache_size + sp_offset_b;
    

    LWSPEC(a_, addr_a, 0); 
    LWSPEC(b_, addr_b, 0);

    sp_c[_idx_(i,j,BLK_DIM)] += a_*b_;
    j++;
    spadRegion = (spadRegion + 1) % NUM_REGIONS;
    REMEM(0); //need to do this collectively for all vector cores if values shared!!

    

  fable3:
    i++;
    j=0;

    // need this jump to create loop carry dependencies, but this should be remove later
    //asm volatile goto("j %l[fable2]\n\t"::::fable2);

  fable4:
    i=0;
    j=0;

  fable5:
    STORE_NOACK(sp_c[_idx_(i,j,BLK_DIM)],c + _idx_(i+i_st, j+j_st, n), 0);
    sp_c[_idx_(i,j,BLK_DIM)]=0;

  fable6:
    j=0;
    i++;

    // need this jump to create loop carry dependencies, but this should be remove later
    //asm volatile goto("j %l[fable5]\n\t"::::fable5);

  fable7:
    j_st++;

  fable8:
    i_st++;
    j_st=0;

    // need this jump to create loop carry dependencies, but this should be remove later
    asm volatile goto("j %l[fable1]\n\t"::::fable1);

  return;
}
*/

void __attribute__((optimize("-fno-inline")))
gemm_vec(DTYPE *a, DTYPE *b, DTYPE *c, int m, int n, int t,
         int m_start, int m_end, int n_start, int n_end, int tid)
{

#ifndef _VEC
  return;
#endif

  int blk_dim = BLK_DIM;
  int a_cache_size = REGION_SIZE / 2;
  int b_cache_size = REGION_SIZE / 2;
  int c_block_size = blk_dim * blk_dim;

  int spadRegion = 0;
  //DTYPE *spAddr = (DTYPE*)getSpAddr(tid,0);

  DTYPE *sp_all[4] = {(DTYPE *)getSpAddr(0, 0), (DTYPE *)getSpAddr(1, 0), (DTYPE *)getSpAddr(2, 0), (DTYPE *)getSpAddr(3, 0)};
  DTYPE *spAddr = sp_all[tid];

#ifdef OUT_SPAD
  DTYPE *sp_c = spAddr + NUM_REGIONS * REGION_SIZE;
#endif

  int offset_x, offset_y;
  int dim_x = 2; //num cpu in a group in x dim
  int dim_y = 2;
  int total_cores = dim_x * dim_y;

  int tid_x = tid % dim_x;
  int tid_y = tid / dim_y;

  offset_x = blk_dim * dim_x;
  offset_y = blk_dim * dim_y;

  DTYPE *sp_a = spAddr;
  DTYPE *sp_b = spAddr + a_cache_size;

  //assuming m_start-m_end is divisble by BLK_DIM
  for (int i0 = m_start; i0 < m_end; i0 += offset_y)
  {
    int i_st = i0 + (tid_y * blk_dim);
    for (int j0 = n_start; j0 < n_end; j0 += offset_x)
    {
      int j_st = j0 + (tid_x * blk_dim);
      for (int k = 0; k < t; k++)
      {

#ifdef IN_SPAD
#if defined(VEC_PFETCH)
        sp_a = spAddr + spadRegion * REGION_SIZE + 0;
        sp_b = spAddr + spadRegion * REGION_SIZE + a_cache_size;
#endif

// fetch a in scratchpad
#ifdef VEC_PFETCH
        for (int i = 0; i < (offset_y / total_cores); i++)
        {
          VPREFETCH(sp_a + i, a + _idx_(k, i0 + (i * total_cores) + tid, m), 0);
        }
// VPREFETCH(sp_a, a + _idx_(k,i0+tid,m), 0);
#else
        for (int i = 0; i < blk_dim; i++)
        {
          sp_a[i] = a[_idx_(k, i + i_st, m)];
        }
#endif

// fetch b in scratchpad
#ifdef VEC_PFETCH
        for (int j = 0; j < (offset_x / total_cores); j++)
        {
          VPREFETCH(sp_b + j, b + _idx_(k, j0 + (j * total_cores) + tid, m), 0);
        }
// VPREFETCH(sp_b, b + _idx_(k,j0+tid,n), 0);
#else
        for (int j = 0; j < blk_dim; j++)
        {
          sp_b[j] = b[_idx_(k, j + j_st, n)];
        }
#endif
#endif

        for (int i = 0; i < blk_dim; i++)
        {
          for (int j = 0; j < blk_dim; j++)
          {
            DTYPE a_, b_;
#ifdef VEC_PFETCH

            int which_sp_a = (tid_y * blk_dim + i) % total_cores;
            int sp_offset_a = (tid_y * blk_dim + i) / total_cores;

            DTYPE *addr_a = sp_all[which_sp_a] + spadRegion * REGION_SIZE + sp_offset_a;

            int which_sp_b = (tid_x * blk_dim + j) % total_cores;
            int sp_offset_b = (tid_x * blk_dim + j) / total_cores;
            DTYPE *addr_b = sp_all[which_sp_b] + spadRegion * REGION_SIZE + a_cache_size + sp_offset_b;

            //LWSPEC(a_, addr_a, 0); //addr according to cores
            //LWSPEC(b_, addr_b, 0);

            a_ = *addr_a;
            b_ = *addr_b;

#elif defined(IN_SPAD)
            a_ = sp_a[i];
            b_ = sp_b[j];
#else
            a_ = a[_idx_(k, i + i_st, m)];
            b_ = b[_idx_(k, j + j_st, n)];
#endif
#ifdef OUT_SPAD
            sp_c[_idx_(i, j, blk_dim)] += a_ * b_;
#else
            c[_idx_(i + i_st, j + j_st, n)] += a_ * b_;
#endif
          }
        }

#ifdef VEC_PFETCH
        spadRegion = (spadRegion + 1) % NUM_REGIONS;
        REMEM(0);
#endif
      }

//store c in DRAM
#ifdef OUT_SPAD
      for (int i = 0; i < blk_dim; i++)
      {
        for (int j = 0; j < blk_dim; j++)
        {
          STORE_NOACK(sp_c[_idx_(i, j, blk_dim)], c + _idx_(i + i_st, j + j_st, n), 0);
          sp_c[_idx_(i, j, blk_dim)] = 0;
        }
      }
#endif
    }
  }
}

void __attribute__((optimize("-fno-inline")))
gemm(DTYPE *a, DTYPE *b, DTYPE *c, int m, int n, int t,
     int m_start, int m_end, int n_start, int n_end, int tid)
{

#if defined(UNBLOCKED_INNER)
  // use inner product unblocked gemm - minimize DRAM writes in this case
  for (int i = m_start; i < m_end; i++)
  {
    for (int j = n_start; j < n_end; j++)
    {
      DTYPE c_temp = 0;
      for (int k = 0; k < t; k++)
      {
        c_temp += a[_idx_(i, k, t)] * b[_idx_(k, j, n)];
      }
      c[_idx_(i, j, n)] = c_temp;
    }
  }

#elif defined(UNBLOCKED_OUTER)
  for (int k = 0; k < t; k++)
  {
    for (int i = m_start; i < m_end; i++)
    {
      for (int j = n_start; j < n_end; j++)
      {
        c[_idx_(i, j, n)] += a[_idx_(i, k, t)] * b[_idx_(k, j, n)];
      }
    }
  }

#else

  int spadRegion = 0;
  DTYPE *spAddr = (DTYPE *)getSpAddr(tid, 0);

#ifdef OUT_SPAD
  DTYPE *sp_c = spAddr + NUM_REGIONS * REGION_SIZE;
#endif

  int offset_x, offset_y;
  const int dim_x = 2; //num cpu in a group in x dim
  const int dim_y = 2;

#if defined(BLOCKED)
  // blocked would mean each core processes its tile in blocks to fit on scratchpad
  offset_x = offset_y = BLK_DIM;
#elif defined(INTERLEAVED)
  offset_x = BLK_DIM * dim_x;
  offset_y = BLK_DIM * dim_y;
#endif

  //assuming m_start-m_end is divisble by BLK_DIM
  for (int i0 = m_start; i0 < m_end; i0 += offset_x)
  {
    for (int j0 = n_start; j0 < n_end; j0 += offset_y)
    {
      for (int k = 0; k < t; k++)
      {

#ifdef IN_SPAD

        DTYPE *sp_a = spAddr + spadRegion * REGION_SIZE + 0;
        DTYPE *sp_b = spAddr + spadRegion * REGION_SIZE + BLK_DIM;

        // fetch a in scratchpad
        for (int i = 0; i < BLK_DIM; i++)
        {
#ifdef VEC_PFETCH
          VPREFETCH(sp_a + i, a + _idx_(i + i0, k, t), 0);
#else
          sp_a[i] = a[_idx_(i + i0, k, t)];
#endif
        }

        // fetch b in scratchpad
        for (int j = 0; j < BLK_DIM; j++)
        {
#ifdef VEC_PFETCH
          VPREFETCH(sp_b + j, b + _idx_(k, j + j0, n), 0);
#else
          sp_b[j] = b[_idx_(k, j + j0, n)];
#endif
        }

#endif

        for (int i = 0; i < BLK_DIM; i++)
        {
          for (int j = 0; j < BLK_DIM; j++)
          {
            DTYPE a_, b_;
#ifdef IN_SPAD
            a_ = sp_a[i];
            b_ = sp_b[j];
#else
            a_ = a[_idx_(i + i0, k, t)];
            b_ = b[_idx_(k, j + j0, n)];
#endif
#ifdef OUT_SPAD
            sp_c[_idx_(i, j, BLK_DIM)] += a_ * b_;
#else
            c[_idx_(i + i0, j + j0, n)] += a_ * b_;
#endif
          }
        }

#ifdef VEC_PFETCH
        spadRegion = (spadRegion + 1) % NUM_REGIONS;
        REMEM(0);
#endif
      }

//store c in DRAM
#ifdef OUT_SPAD
      for (int i = 0; i < BLK_DIM; i++)
      {
        for (int j = 0; j < BLK_DIM; j++)
        {
          STORE_NOACK(sp_c[_idx_(i, j, BLK_DIM)], c + _idx_(i + i0, j + j0, n), 0);
          sp_c[_idx_(i, j, BLK_DIM)] = 0;
        }
      }
#endif
    }
  }

#endif
}

// actual kernel
void kernel(
    DTYPE *a, DTYPE *b, DTYPE *c, int m, int n, int t,
    int tid_x, int tid_y, int dim_x, int dim_y)
{

  // start recording all stats (all cores)
  // use the last thread, b/c this wakes up last?
  if (tid_x == 0 && tid_y == 0)
  {
    stats_on();
  }

#if defined _VEC && defined VPF
  int m_start = 0;
  int n_start = 0;
  int m_end = m;
  int n_end = n;
#else
// figure out which block this thread should do
#ifdef INTERLEAVED
  int m_start = tid_y * BLK_DIM;
  int n_start = tid_x * BLK_DIM;

  int m_end = m - BLK_DIM * (dim_y - tid_y - 1);
  int n_end = n - BLK_DIM * (dim_x - tid_x - 1);
#else
  int m_start = tid_y * (m / dim_y);
  int n_start = tid_x * (n / dim_x);

  // get end with remainders
  int m_chunk = (int)(m / dim_y);
  int n_chunk = (int)(n / dim_x);
  if (tid_x == dim_x - 1)
  {
    n_chunk += n % dim_x;
  }
  if (tid_y == dim_y - 1)
  {
    m_chunk += m % dim_y;
  }
  int m_end = m_start + m_chunk;
  int n_end = n_start + n_chunk;
#endif
#endif

  int vtid = 0;
  int is_da = 0;
  int vtid_x, vtid_y;

  int tid = tid_x + tid_y * dim_x;
  int orig_x, orig_y, master_x, master_y;

  int vdim_x = 2;
  int vdim_y = 2;

#if defined USE_VECTOR_SIMD
  if (tid == 1)
    vtid = 0;
  if (tid == 2)
    vtid = 1;
  if (tid == 5)
    vtid = 2;
  if (tid == 6)
    vtid = 3;
  if (tid == 0)
    is_da = 1;
  if (tid == 0 || tid == 1 || tid == 2 || tid == 5 || tid == 6)
  {
    orig_x = 1;
    orig_y = 0;
    master_x = 0;
    master_y = 0;
  }
  else
  {
    return;
  }

  vtid_x = vtid % vdim_x;
  vtid_y = vtid / vdim_y;

#else

  int orig_x = 0;
  int orig_y = 0; //only have 1 group for now
#endif

  //printf("iterations %d %d\n", n_end - n_start, m_end - m_start);

  int prefetchMask = (NUM_REGIONS << PREFETCH_NUM_REGION_SHAMT) | (REGION_SIZE << PREFETCH_REGION_SIZE_SHAMT);
  PREFETCH_EPOCH(prefetchMask);
  //pthread_barrier_wait(&start_barrier);

#if defined USE_VECTOR_SIMD
  int mask = getSIMDMask(master_x, master_y, orig_x, orig_y, vtid_x, vtid_y, vdim_x, vdim_y, is_da);
  gemm_vec_simd(mask, a, b, c, m, n, t, m_start, m_end, n_start, n_end, vtid_x, vtid_y, vtid, tid);
#elif defined _VEC
  int mask = 0;
  mask = getVecMask(orig_x, orig_y, tid_x, tid_y, dim_x, dim_y);

  // configure
  VECTOR_EPOCH(mask);
#if defined VPF
  gemm_vec(a, b, c, m, n, t, m_start, m_end, n_start, n_end, tid);
#else
  gemm(a, b, c, m, n, t, m_start, m_end, n_start, n_end, tid); // for non VPF case, algorithm is same - use same kernel to compare baseline and vec config
#endif

  //pthread_barrier_wait(&start_barrier);
  // deconfigure
  VECTOR_EPOCH(0);

#else
  gemm(a, b, c, m, n, t, m_start, m_end, n_start, n_end, tid);
#endif
}

// helper functions
Kern_Args *construct_args(DTYPE *a, DTYPE *b, DTYPE *c, int m, int n, int t,
                          int tid_x, int tid_y, int dim_x, int dim_y)
{

  Kern_Args *args = (Kern_Args *)malloc(sizeof(Kern_Args));

  args->a = a;
  args->b = b;
  args->c = c;
  args->m = m;
  args->n = n;
  args->t = t;
  args->tid_x = tid_x;
  args->tid_y = tid_y;
  args->dim_x = dim_x;
  args->dim_y = dim_y;

  return args;
}

void *pthread_kernel(void *args)
{
  // guarentee one thread goes to each core, by preventing any threads
  // from finishing early

  pthread_barrier_wait(&start_barrier);

  // call the spmd kernel
  Kern_Args *a = (Kern_Args *)args;

  kernel(a->a, a->b, a->c, a->m, a->n, a->t,
         a->tid_x, a->tid_y, a->dim_x, a->dim_y);

  if (a->tid_x == 1 && a->tid_y == 1)
  {
    stats_off();
  }

  return NULL;
}
