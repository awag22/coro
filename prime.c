/*
 * Prime Number Generator using uthreads -- no conditionals
*/
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include "uthread.h"
#include "uthread_mutex_cond.h"

int *numbers=NULL;

union fargs {
    void *p;
    struct {
        int i;
        int num;
    } a;
};

void *mid(void *vp)
{
    union fargs args;
    args.p = vp;
	int prime = 0;
	int n = args.a.i;
	int numprimes = args.a.num;
	if (n==0) {
		int num = 2;
		while (1) {
			if (numbers[0] >= 0 ) numbers[1] = num++;
			else break;
	    	uthread_yield();
        }
    } else if (n == numprimes) {
		while (1) {
			if (numbers[numprimes] != 0) {
				int i;
				for (i=0; i<numprimes; i++) numbers[i] = -1;  // write stop signal
				break;
			}
			uthread_yield();
		}
    } else {
        int first = 1;
		while (1) {
			if ( (numbers[n] > 0) && first) { 
				prime = numbers[n]; 
				first = 0;
			} else if (numbers[n] > 0) {
				if (numbers[n] % prime) 
					{ numbers[n+1]=numbers[n]; //not divisible 
				} else ; // do nothing
			} else if (numbers[n] == 0) ;//do nothing
			else break; // under 0, time to terminate
		    uthread_yield();
		}
	}
	printf("Finished Process at %d with prime=%d\n",n,prime);
	return NULL;
}

int main (int argc, char** argv) 
{
  if (argc < 2) {
    printf("usage: prime num  --- needs one parameter\n");
    exit(-1);
  }
  int numprimes = atoi(argv[1]);
  if ( (numbers = (int *) calloc(sizeof(int),numprimes+1)) < 0) {
    printf("bad return from calloc\n");
    exit(-1);
  }
  uthread_t *midthread;
  if ( (midthread = (uthread_t *) malloc(sizeof(uthread_t)*numprimes)) < 0) {
    printf("bad return from malloc for threads\n");
    exit(-1);
  }
  
  int i;
  uthread_init(1);
  for (i=0;i<=numprimes;i++) {
	numbers[i] = 0;
	union fargs args;
	args.a.i = i;
	args.a.num = numprimes;
    midthread[i] = uthread_create ((void *)mid,args.p);
  }
  for (i=0;i<numprimes;i++) {
    uthread_join(midthread[i],0);
  }
  exit(0);
}
