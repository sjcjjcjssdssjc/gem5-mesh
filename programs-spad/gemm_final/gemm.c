#include <stdlib.h>
#include <stdio.h>

#include "pthread_launch.h"
#include "gemm.h"
#include "spad.h"
#include "bind_defs.h"
#include "token_queue.h"
#include "group_templates.h"
#include "reduction.h"

#include "gemm_kernel.h"

void transpose_manycore(DTYPE *a, int a_row, int a_col, DTYPE *aT, int ptid, int pdim){

  int start = (ptid + 0) * a_col / pdim;
  int end = (ptid + 1) * a_col / pdim;

  for(int i=start; i<end; i++){
    for(int j=0; j<a_row; j++){
      aT[i*a_row+j] = a[j*a_col+i];
    }
  }

}


// actual kernel
void kernel(
    DTYPE *a, DTYPE *aT, DTYPE *b, DTYPE *c, int m, int n, int t,
    int ptid_x, int ptid_y, int pdim_x, int pdim_y)
{

  // start recording all stats (all cores)
  // use the last thread, b/c this wakes up last?
  if (ptid_x == 0 && ptid_y == 0)
  {
    stats_on();
  }

  int ptid = ptid_x + ptid_y * pdim_x;
  int pdim = pdim_x * pdim_y;
  int m_start = 0;
  int m_end = 0;
  int n_start = 0;
  int n_end = 0;
  int vdim;
  template_info_t tinfo;

  transpose_manycore(a,m,t,aT,ptid,pdim);
  a=aT;

  #ifdef _VEC
  #if VEC_LEN==4
  tinfo = init_template_4x4_2x2();
  #elif VEC_LEN==16
  tinfo = init_template_8x8_4x4();
  #endif
  core_config_info_t cinfo = vector_group_template(ptid_x, ptid_y, pdim_x, pdim_y, &tinfo);

  vdim = cinfo.vdim_x*cinfo.vdim_y;
  int *ptid_group = getSpAddr(ptid,NUM_REGIONS * REGION_SIZE + BLK_DIM*BLK_DIM + 10);

  WORK_DIV(m,n)

  #ifdef SHARING
  if(cinfo.used){
    for(int i=0; i<cinfo.vdim_y;i++){
      for(int j=0; j<cinfo.vdim_x; j++){
        ptid_group[i*cinfo.vdim_x+j] = get_ptid_from_group(&tinfo, cinfo.unique_id,j,i,pdim_x);
        // if (ptid==0) printf("Ptid: %d\n", ptid_group_[i*vdim_x+j]);
      }
    }
  }
  #endif


  
#else
  core_config_info_t cinfo = manycore_template(ptid_x, ptid_y, pdim_x, pdim_y);
  
  //do work division here

  m_start  = ptid_y * BLK_DIM;
  n_start  = ptid_x * BLK_DIM;
#endif

// get behavior of each core
  #ifdef _VEC
  int mask = getSIMDMask(&cinfo);
  #elif defined MANYCORE_PREFETCH
  int mask = getDebugMask(&cinfo);
  #else
  int mask = 0;
  #endif


  // region based mask for scratchpad
#ifdef _VEC
  SET_PREFETCH_MASK(NUM_REGIONS,REGION_SIZE,&start_barrier);
#elif defined MANYCORE_PREFETCH
  SET_PREFETCH_MASK(NUM_REGIONS,REGION_SIZE,&start_barrier);
#endif

  // if (cinfo.used == 0) return;


MOVE_STACK_ONTO_SCRATCHPAD();


#if defined _VEC
if (cinfo.used)
  tril_gemm_vec(mask, a, b, c, m, n, t, m_start, m_end, n_start, n_end, cinfo.vtid_x, cinfo.vtid_y, cinfo.vtid, ptid);
#else
if (cinfo.used){
  VECTOR_EPOCH(mask);
  gemm_manycore(a, b, c, m, n, t, m_start, n_start, ptid, pdim_x, pdim_y);
}
#endif
  RECOVER_DRAM_STACK();
}

// helper functions
Kern_Args *construct_args(DTYPE *a, DTYPE *aT, DTYPE *b, DTYPE *c, int m, int n, int t,
                          int tid_x, int tid_y, int dim_x, int dim_y)
{

  Kern_Args *args = (Kern_Args *)malloc(sizeof(Kern_Args));

  args->a = a;
  args->aT = aT;
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

  kernel(a->a, a->aT, a->b, a->c, a->m, a->n, a->t,
         a->tid_x, a->tid_y, a->dim_x, a->dim_y);

  pthread_barrier_wait(&start_barrier);
  if (a->tid_x == 1 && a->tid_y == 1)
  {
    stats_off();
  }

  return NULL;
}
