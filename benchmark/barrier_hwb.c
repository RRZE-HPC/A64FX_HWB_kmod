// skeleton
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <omp.h>
#include "timing.h"

#include <sched.h>

#include <fujitsu_hwb.h>

static int _bd;

double workfunc(double y) {
    return exp(y);
}


double func_with_barrier(int win) {
	double x=0.0,y=3.04;
    fhwb_sync(win);
    x = workfunc(y);
    if(x<0.)
      printf("%.15lf",x);
    return x;
}

double func_without_barrier() {
	double x=0.0,y=3.04;
    x = workfunc(y);
    if(x<0.)
      printf("%.15lf",x);
    return x;
}


int main(int argc, char** argv) {

  double wct_wstart,wct_wostart,wct_wend,wct_woend,wct_mid,wct_end,cput_start,cput_end;
  int id,nt;
  int NITER;
  double t = 0, clockspeed;
  cpu_set_t myset;
  int ret = 0;

  if(argc!=2) {
	fprintf(stderr,"Usage: %s <clock_in_GHz>\n", argv[0]);
    exit(1);
  }
  clockspeed = atof(argv[1])*1.0e9;
  ret = sched_getaffinity(0, sizeof(cpu_set_t), &myset);
  ret = fhwb_init(sizeof(cpu_set_t), &myset);
  if (ret < 0)
  {
    fprintf(stderr,"Error init barrier\n");
    exit(1);
  }
  _bd = ret;
  NITER=1;
  do {
#pragma omp parallel
{
    // time measurement
    cpu_set_t set;
    int k;
    CPU_ZERO(&set);
	CPU_SET(omp_get_thread_num(), &set);
	ret = sched_setaffinity(0, sizeof(cpu_set_t), &set);
	if (ret < 0)
  {
    fprintf(stderr,"Error setting cpuset\n");
    exit(1);
  }
	ret = fhwb_assign(_bd, -1);
	if (ret < 0)
  {
    fprintf(stderr,"Error assign barrier\n");
    exit(1);
  }
#pragma omp single
    timing(&wct_wstart, &cput_start);
    for(k=0; k<NITER; ++k) {
      func_with_barrier(ret);
    }
#pragma omp single
    timing(&wct_wend, &cput_end);
    ret = fhwb_unassign(_bd);
    if (ret < 0)
  {
    fprintf(stderr,"Error unassign barrier\n");
    exit(1);
  }



#pragma omp single
    timing(&wct_wostart, &cput_start);
    for(k=0; k<NITER; ++k) {
      func_without_barrier();
    }
#pragma omp single
    timing(&wct_woend, &cput_end);
} // end parallel
    NITER = NITER*2;
  } while (wct_woend-wct_wostart<0.001);

  NITER = NITER/2;
  ret = fhwb_fini(_bd);
  if (ret < 0)
  {
    fprintf(stderr,"Error finalize barrier\n");
    exit(1);
  }
  printf("NITER: %d, time: %.3lf, time w/o b: %.3lf, barrier: %.1lf cy\n",NITER,(wct_wend-wct_wstart),(wct_woend-wct_wostart),((wct_wend-wct_wstart)-(wct_woend-wct_wostart))/NITER*clockspeed);
  
  return 0;
}
