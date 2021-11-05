#define _XOPEN_SOURCE

#ifndef __SUT_H__
#define __SUT_H__

#include <stdbool.h>
#include <ucontext.h>
#include <unistd.h>

#define MAX_THREADS 32
#define THREAD_STACK_SIZE 1024 * 64

typedef void (*sut_task_f)();

typedef struct __threadDescriptor {
  int thread_id;
  char *thread_stack;
  void *thread_func;
  ucontext_t thread_context;
} threadDescriptor;

extern threadDescriptor thread_array[MAX_THREADS];
extern int thread_count, current_thread;
extern ucontext_t parent;

extern struct queue task_queue, io_queue;

void sut_init();
bool sut_create(sut_task_f fn);
void sut_yield();
void sut_exit();
int sut_open(char *dest);
void sut_write(int fd, char *buf, int size);
void sut_close(int fd);
char *sut_read(int fd, char *buf, int size);
void sut_shutdown();

int get_thread_id(threadDescriptor *thread_descriptor);

#endif
