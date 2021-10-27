#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// project-specific libs
#include "other/queue/queue.h"
#include "sut.h"

threadDescriptor thread_array[MAX_THREADS];
int thread_count, current_thread;
ucontext_t parent;
struct queue task_queue, io_queue;

void sut_init() {
  thread_count = 0;
  current_thread = 0;

  // create and init each task and io queue
  task_queue = queue_create();
  queue_init(&task_queue);
  io_queue = queue_create();
  queue_init(&io_queue);
}

// TODO: check if library task definition fits the prototype
bool sut_create(void(sut_task_f)()) {

  // control max number of thread available
  if (thread_count >= MAX_THREADS) {
    printf("Error: active thread limit was reached.\n");
    return false;
  }

  // get next thread descriptor in array of available thread descriptors
  thread_descriptor = &(thread_array[thread_count]);
  // retrieve current context for thread
  getcontext(&(thread_descriptor->thread_context));
  // update the thread's id
  thread_descriptor->thread_id = thread_count;
  // allocate stack to thread
  thread_descriptor->thread_stack = (char *)malloc(THREAD_STACK_SIZE);
  // set stack pointer to allocated stack
  thread_descriptor->thread_context.uc_stack.ss_sp =
      thread_descriptor->thread_stack;
  // set size of allocated stack for thread
  thread_descriptor->thread_context.uc_stack.ss_size = THREAD_STACK_SIZE;
  thread_descriptor->thread_context.uc_link = 0;
  thread_descriptor->thread_context.uc_stack.ss_flags = 0;
  // set thread function to be executed (which implements sut API calls)
  thread_descriptor->thread_func = sut_task_f;

  // make this thread's context based on newly built thread descriptor
  makecontext(&(thread_descriptor->thread_context), sut_task_f, 1,
              thread_descriptor);

  /*****TODO: implement queuing thread to task queue***/

  // append thread descriptor to task queue
  struct queue_entry *task =
      queue_new_node(&thread_desciptor) queue_insert_tail(&task_queue, task);

  // if no tasks are running, run the newly added queue
  if (thread_descriptor->thread_id == 0) {
    // swap context between the parent thread and newly created thread
    swapcontext(&parent, &(thread_array[thread_count].thread_context))
    // note that the thread remains in the same position in the queue
  }
  /***END IMPLEMENTATION OF RUNNING TASK QUEUES.**/

  thread_count++;

  return true;
}

void sut_yield() {
  // pop task queue to get next thread
  // whenever a task is poped - can work as a circularly linked list
}

void sut_exit() {}

int sut_open(char *dest) {}

void sut_write(int fd, char *buf, int size) {}

void sut_close(int fd) {}

char *sut_read(int fd, char *buf, int size) {}

void sut_shutdown() {}
