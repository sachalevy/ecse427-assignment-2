#define _XOPEN_SOURCE

#ifndef __YAUTHREAD_H__
#define __YAUTHREAD_H__

#include <ucontext.h>

// maximum number of threads alive simultaneously
#define MAX_THREADS                        32
// delimit memory allocated to each thread
#define THREAD_STACK_SIZE                  1024*64

// types of resource allocations
#define RR                                 1   // round robin
#define FCFS                               2   // first come first served

// round robin rotates workload allocation, everyone gets 2 seconds?
#define RR_QUANTUM                         2   // in seconds


// prototype definition of a thread descriptor structure? 
typedef struct __threaddesc
{
	// unique(?) thread id
	int threadid;
	// reference to memory address of the thread
	char *threadstack;
	// task attached by user to the executing (or to be executed) thread
	void *threadfunc;
	// manage the thread's context - what's the stack use?
	ucontext_t threadcontext;
} threaddesc;


// create array named threadarr with MAX_THREADS elements of type thread descriptor 
extern threaddesc threadarr[MAX_THREADS];
// init the current number of threads as well as ref to current thread id?
extern int numthreads, curthread;
// init global variable for context of the parent thread - main thread of process
extern ucontext_t parent;



// some function prototypes...

void initYAUThreads();
int YAUSpawn( void (threadfunc)(threaddesc *arg) );
void startYAUThreads(int sched);
int getYAUThreadid(threaddesc *th);


// not yet implemented..
void YAUWaitall();




#endif

