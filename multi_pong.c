//
// Written by Mike Feeley, University of BC, 2010-2014
// Do not redistribute any portion of this code without permission.
//

#include <stdlib.h>
#include <stdio.h>
#include "uthread.h"

#ifndef NUM_ITERATIONS
#define NUM_ITERATIONS 10000
#endif
#ifndef NUM_THREADS
#define NUM_THREADS    10
#endif
#ifndef NUM_PROCESSORS
#define NUM_PROCESSORS 2
#endif

void* op (void* str) {
  int i;
  for (i=0; i<NUM_ITERATIONS; i++) {
    printf ("%s %d\n", (char*) str, i);
    uthread_yield ();
  }
  printf ("0x%lx done\n",(uintptr_t) uthread_self());
  return NULL;
}

int main (int argc, char** argv) {
  uthread_t t[NUM_THREADS];
  char      s[NUM_THREADS][100];
  int i;
  
  uthread_init (NUM_PROCESSORS);
  for (i=0; i<NUM_THREADS; i++) {
    sprintf (s[i], "T%d", i);
    t[i] = uthread_create (op, s[i]);
    printf ("t %d: 0x%lx\n",i, (uintptr_t) t[i]);
  }
  for (i=0; i<NUM_THREADS; i++) {
    printf("j%d ... ",i);fflush(stdout);
    uthread_join (t[i], 0);
    printf("j\n");fflush(stdout);
  }
}
