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
#include "uthread_sem.h"
#include "chan.h"

#define FALSE 0
#define TRUE !FALSE

union fargs {
    void *p;
    struct {
        int i;
        int num;
    } a;
};

int done = FALSE;
chan_t **pchan = NULL;

void *gen(void *ptr) { 
    int val=2;
    while (!done) 
    	chan_send_int32(pchan[0],val++);    // send next value
    chan_send_int32(pchan[0],0); // send zero to kill off everything
    return NULL;
}

void *stop(void *ptr) { 
    union fargs args;
    args.p = ptr;
    int prime=0;
    chan_recv_int32(pchan[args.a.num],&prime);  // final prime
    done = TRUE; 
    int val=TRUE;
    while (val) 
        chan_recv_int32(pchan[args.a.num],&val);  // drain the pipeline  
    printf("prime[%d]=%d",args.a.i,prime);
    return NULL;
}

void *prime(void *ptr) { 
    union fargs args;
    args.p = ptr;
    int prime = 0;
    chan_recv_int32(pchan[args.a.i],&prime);  // get the first value which is the prime value
    int val=TRUE;
    while (val) {
        chan_recv_int32(pchan[args.a.i],&val);  
        if (!val) break;  // zero means it is draining  
        else if ( val%prime ) chan_send_int32(pchan[args.a.i+1],val);    // send number  
        else ;
    }
    chan_send_int32(pchan[args.a.i+1],0);    // finishing
    printf("prime[%d]=%d ",args.a.i,prime);
    return NULL;
}

int main (int argc, char** argv) 
{
  if (argc < 2) {
    printf("usage: prime num  --- needs one parameter\n");
    exit(-1);
  }
  int numprimes = atoi(argv[1]);
  uthread_t *primethread;
  if ( (primethread = (uthread_t *) malloc(sizeof(uthread_t)*numprimes)) < 0) {
    printf("bad return from malloc for threads\n");
    exit(-1);
  }
  if ( (pchan = (chan_t **) malloc(sizeof(chan_t)*(numprimes+1))) < 0 ) {
    printf("bad return from malloc for channels\n");
    exit(-1);
  }
  
  int i;
  uthread_init(1);
  union fargs args;
  args.a.num = numprimes;
  for (i=0;i<numprimes;i++) {
	args.a.i = i;
	pchan[i] = chan_init(0);
    primethread[i] = uthread_create ((void *)prime,args.p);
  }
  pchan[numprimes] = chan_init(0);
  uthread_t stopthread = uthread_create ((void *)stop,args.p);  // needs the value of total number primes
  uthread_t genthread = uthread_create ((void *)gen,NULL);
  for (i=0;i<numprimes;i++)  uthread_join(primethread[i],0);
  uthread_join(genthread,0);
  uthread_join(stopthread,0);
  printf("\n");
  exit(0);
}
