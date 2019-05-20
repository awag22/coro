/*
 * Prime number generator using libuv -- only using an event loop with callbacks
*/
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <uv.h>

// information belonging to each coroutine ith coroutine owns ith values
int *numbers=NULL;     // list of prime numbers, initially set p[0] to 2
int *pnum = NULL;      // list of numbers 

uv_async_t *primer = NULL;
int numprimes = 0;

union fargs {
    void *p;
    struct {
        int i;
        int num;
    } a;
};

void callbk(uv_async_t* handle) {
    union fargs args;
    args.p = uv_handle_get_data((uv_handle_t *)handle);  
    //printf("Running coroutine %d \n",args.a.i);
    if ( args.a.i == 0) {  // generate the list of numbers
        numbers[args.a.i]++;        
        uv_async_send(&(primer[1]));
    } else if ( args.a.i == numprimes-1 ) {
        uv_stop(uv_default_loop()); 
    } else {  
        if (numbers[args.a.i-1] && !pnum[args.a.i]) {
            // found a prime --- previous not zero and no prime set 
            pnum[args.a.i] = numbers[args.a.i-1];  // prime is the previous value
            uv_async_send(&(primer[0]));   // can return to zero
        } else if ( pnum[args.a.i] && (numbers[args.a.i-1] % pnum[args.a.i])) {
            // not divisible by prime -- copy the result into my number 
            numbers[args.a.i] =  numbers[args.a.i-1];
            uv_async_send(&(primer[args.a.i+1]));
        } // is divisible by the prime go back to start
        else uv_async_send(&(primer[0])); 
    }
}

int main (int argc, char** argv) 
{
    if (argc < 2) {
        printf("usage: prime num  --- needs one parameter\n");
        exit(-1);
    }
    numprimes = atoi(argv[1]);
    
    if  ( ((numbers = (int *) calloc(sizeof(int),numprimes)) < 0)
            || ((pnum = (int *) calloc(sizeof(int),numprimes)) < 0)
            || ((primer = (uv_async_t *) calloc(sizeof(uv_async_t),numprimes)) < 0) ) 
    {
        printf("bad return from calloc\n");
        exit(-1);
    }     
    
    union fargs args;
    int i;
    for (i=0; i<numprimes;i++) {
        args.a.i = i; args.a.num = 0;
        uv_handle_set_data((uv_handle_t *)&(primer[i]),args.p);
        uv_async_init(uv_default_loop(), &(primer[i]), &callbk);
    }
    numbers[0]=1;
    pnum[0] = 1; // first prime
    uv_async_send(&(primer[0]));
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);
    
    printf("Primes: ");
    for (i=0; i<numprimes; i++) printf("%d ",pnum[i]);
    printf("\n");
}

