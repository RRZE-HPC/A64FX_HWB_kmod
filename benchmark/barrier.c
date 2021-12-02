// skeleton
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "timing.h"


double workfunc(double y) {
    return exp(y);
}


double func_with_barrier() {
	double x=0.0,y=3.04;
#pragma omp barrier
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


  if(argc!=2) {
	fprintf(stderr,"Usage: %s <clock_in_GHz>\n", argv[0]);
    exit(1);
  }
  clockspeed = atof(argv[1])*1.0e9;

  NITER=1;
  do {
#pragma omp parallel
{
    // time measurement
    int k;
#pragma omp single
    timing(&wct_wstart, &cput_start);
    for(k=0; k<NITER; ++k) {
      func_with_barrier();
    }
#pragma omp single
    timing(&wct_wend, &cput_end);



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

  printf("NITER: %d, time: %.3lf, time w/o b: %.3lf, barrier: %.1lf cy\n",NITER,(wct_wend-wct_wstart),(wct_woend-wct_wostart),((wct_wend-wct_wstart)-(wct_woend-wct_wostart))/NITER*clockspeed);
  
  return 0;
}
