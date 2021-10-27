/*Defines expected behaviour of the SUT library.*/
#ifndef __SUT_H__
#define __SUT_H__
#include <other/queue/queue.h>
#include <stdbool.h>
#include <ucontext.h>
#include <unistd.h>

// NOTE: defining concepts similar to the YAUThreads lib
// maximum number of threads alive simultaneously
#define MAX_THREADS 32
// delimit memory allocated to each thread
#define THREAD_STACK_SIZE 1024 * 64

typedef void (*sut_task_f)();

// prototype definition of a thread descriptor structure?
typedef struct __threadDescriptor {
  // unique(?) thread id
  int thread_id;
  // reference to memory address of the thread
  char *thread_stack;
  // task attached by user to the executing (or to be executed) thread
  void *thread_func;
  // manage the thread's context - what's the stack use?
  ucontext_t thread_context;
} threadDescriptor;

// create array named threadarr with MAX_THREADS elements of type thread
// descriptor
extern threadDescriptor thread_array[MAX_THREADS];
// init the current number of threads as well as ref to current thread id?
extern int thread_count, current_thread;
// init global variable for context of the parent thread - main thread of
// process
extern ucontext_t parent;

// define I/O and Task queues
extern struct queue task_queue, io_queue;

// prototype for library functions to be implemented
/**DO NOT CHANGE BELOW.**/
void sut_init();
bool sut_create(sut_task_f fn);
void sut_yield();
void sut_exit();
int sut_open(char *dest);
void sut_write(int fd, char *buf, int size);
void sut_close(int fd);
char *sut_read(int fd, char *buf, int size);
void sut_shutdown();

// additionally...
int get_thread_id(threadDescriptor *thread_descriptor);

#endif
