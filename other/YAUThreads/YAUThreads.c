#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include "YAUThreads.h"

// initialize threadarr: arr of thread descriptors
threaddesc threadarr[MAX_THREADS];
int numthreads, curthread;
ucontext_t parent;


void initYAUThreads()
{
	// init num of thread and current thread id
	numthreads = 0;
	curthread = 0;
}


int YAUSpawn( void (threadfunc)() )
{
	// init a new variable of type threaddescriptor, named tdescptr
	threaddesc *tdescptr;

	if (numthreads >= 32)
	{
		printf("FATAL: Maximum thread limit reached... creation failed! \n");
		return -1;
	}

	// get current next slot in the thread arr for thread descript
	// as starting with numthreads == 0, blank slate
	tdescptr = &(threadarr[numthreads]);
	// retrieve context of newly loaded thread descriptor -> why do we need to getcontext if blank?
	getcontext(&(tdescptr->threadcontext));
	// update the thread descriptor's id with the current thread count
	tdescptr->threadid = numthreads;
	// allocate a fresh stack for the current thread
	tdescptr->threadstack = (char *)malloc(THREAD_STACK_SIZE);
	// switch stack pointer to freshly created stack
	tdescptr->threadcontext.uc_stack.ss_sp = tdescptr->threadstack;
	// set stack size of thread context for new thread of previously defined size
	tdescptr->threadcontext.uc_stack.ss_size = THREAD_STACK_SIZE;
	// ss & link? something to do with interrupts?
	tdescptr->threadcontext.uc_link = 0;
	tdescptr->threadcontext.uc_stack.ss_flags = 0;
	// set the current thread's function to the spawning task
	tdescptr->threadfunc = threadfunc;

	// make the thread's context with the current function,
	// allocated context, and thread descriptor struct (is tdescptr some implementation following standards?)
	makecontext(&(tdescptr->threadcontext), threadfunc, 1, tdescptr);

	// increment number of running threads
	numthreads++;

	return 0;
}


void handle_timerexpiry()
{
        struct sigaction handler;
	int nxtthread, curthreadsave;

	// need to set this anytime will want to create a new thread and swap into it?
        handler.sa_handler = handle_timerexpiry;
        sigaction(SIGALRM, &handler, NULL);
        alarm(RR_QUANTUM);

	nxtthread = (curthread +1) % numthreads;

	curthreadsave = curthread;
	curthread = nxtthread;
	// swap the contexts between the the current thrad, and the next thread
	swapcontext(&(threadarr[curthreadsave].threadcontext),
		&(threadarr[nxtthread].threadcontext));
	// does swap automatically save the thread's context? is that not necessary because
	// they have different stack blocks allocated?
}


void startYAUThreads(int sched)
{

	struct sigaction handler;

	// here start threads in a round robin mode, if any threads available
	if (sched == RR && numthreads > 0)
	{
		// round robin iterates through the threads allocating timeslots
		handler.sa_handler = handle_timerexpiry;
		// set an alarm to go off at RR_QUANTUM - about 2 seconds after start
		sigaction(SIGALRM, &handler, NULL);
		alarm(RR_QUANTUM);

		// swap the context between the first thread, init chain of swaps
		swapcontext(&parent, &(threadarr[curthread].threadcontext));
	}

	// can effectively only be called once?
}


int getYAUThreadid(threaddesc *th)
{
	return th->threadid;
}
