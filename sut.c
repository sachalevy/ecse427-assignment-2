#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

#include "sut.h"
#include "queue.h"

int yield;

threadDescriptor thread_array[MAX_THREADS];
int thread_count, current_thread, current_thread_id;
ucontext_t parent;
pthread_t thid_task_exec, thid_io_exec;
void *io_ret, *task_ret;
struct queue task_queue, io_queue, tid_queue;


void handle_yield() {
	/*Upon SIGUSR1 being triggered, take a look at current process in line.*/
	struct queue_entry *current_thread_ptr = queue_pop_head(&task_queue);

	printf("Caught a task yiekd\n");
	
	// can only ever yield user thread if there is one
	if (current_thread_ptr) {
		printf("switching to next task\n");
		struct queue_entry *next_thread_ptr = queue_peek_front(&task_queue);
		printf("id of the current thread %d\n", *(int*)current_thread_ptr->data);
		printf("id of the next thread %d\n", *(int*)next_thread_ptr->data);
		if (next_thread_ptr) {
			printf("found a valid next ptr so switching to it\n");
			swapcontext(&((*(threadDescriptor*)current_thread_ptr->data).thread_context),
			&((*(threadDescriptor*)next_thread_ptr->data).thread_context));
		}
		queue_insert_tail(&task_queue, current_thread_ptr);
	}
}

void handle_exit() {
	/*Upon SIGUSR2 being triggered, stop current task and remove it from queue.*/
	struct queue_entry *current_thread_ptr = queue_pop_head(&task_queue);

	printf("caught a sig exit\n");
	
	// can only ever exit user thread if there is one
	if (current_thread_ptr) {
		struct queue_entry *next_thread_ptr = queue_peek_front(&task_queue);
		if (next_thread_ptr) {
			swapcontext(&((*(threadDescriptor*)current_thread_ptr->data).thread_context),
				&((*(threadDescriptor*)next_thread_ptr->data).thread_context));
		} else {
			swapcontext(&((*(threadDescriptor*)current_thread_ptr->data).thread_context),
				&parent);
		}
	}

	thread_count--;
}

void *task_executor(void *arg) {
	struct sigaction yield_handler;
	yield_handler.sa_handler = handle_yield;
	sigaction(SIGUSR1, &yield_handler, NULL);

	struct sigaction exit_handler;
	exit_handler.sa_handler = handle_exit;
	sigaction(SIGUSR2, &exit_handler, NULL);
	
	bool is_running = false;
	while (true) {
		struct queue_entry *current_thread_ptr = queue_peek_front(&task_queue);
		printf("Found something interesting %d\n", !is_running && current_thread_ptr);
		if (!is_running && current_thread_ptr) {
			printf("Hello from here\n");
			//swapcontext(&parent,  &((*(threadDescriptor*)current_thread_ptr->data).thread_context));
			swapcontext(&parent,  &(thread_array[0].thread_context));
			is_running = true;
		} else {
			nanosleep((const struct timespec[]){{0, 500000000L}}, NULL);
		}
	}
}

bool sut_init() {
	thread_count = 0;
	current_thread = 0;
	current_thread_id = 0;

	task_queue = queue_create();
	queue_init(&task_queue);

	if (pthread_create(&thid_task_exec, NULL, task_executor, NULL) != 0) {
		perror("Error creating the task executor thread.\n");
		exit(1);
	}

	return true;
}

bool sut_create(sut_task_f sut_task) {
	if (thread_count > MAX_THREADS) {
		return false;
	}

	// init thread descriptor for task
	threadDescriptor *thread_descriptor;
	//thread_descriptor = &(thread_array[*(int*)thread_id->data]);
	thread_descriptor = &(thread_array[thread_count]);
	getcontext(&(thread_descriptor->thread_context));
	thread_descriptor->thread_id = thread_count;
	thread_descriptor->thread_stack = (char *)malloc(THREAD_STACK_SIZE);
	thread_descriptor->thread_context.uc_stack.ss_sp = thread_descriptor->thread_stack;
	thread_descriptor->thread_context.uc_stack.ss_size = THREAD_STACK_SIZE;
	thread_descriptor->thread_context.uc_link = 0;
	thread_descriptor->thread_context.uc_stack.ss_flags = 0;
	thread_descriptor->thread_func = sut_task;
	makecontext(&(thread_descriptor->thread_context), sut_task, 1, thread_descriptor);

	printf("Inserting new function to be executed in task queue.\n");
	struct queue_entry *task_thread_id = queue_new_node(&thread_descriptor);
	//struct queue_entry *task_id = queue_new_node(&thread_count);
	queue_insert_tail(&task_queue, task_thread_id);

	printf("current thread id %d \n", (*(threadDescriptor*)task_thread_id->data).thread_id);

	thread_count+=2;
	printf("current thread id %d \n", (threadDescriptor*)task_thread_id->data);

	return true;
}

void sut_yield() {
	pthread_kill(thid_task_exec, SIGUSR1);
}

void sut_exit() {
	pthread_kill(thid_task_exec, SIGUSR2);
}

int sut_open(char *dest) {


}

void sut_write(int fd, char *buf, int size) {

}

void sut_close(int fd) {

}

char *sut_read(int fd, char *buf, int size) {

}

void sut_shutdown() {
	void *retval;
	if (pthread_join(thid_task_exec, &retval) != 0) {
		perror("Error while joining executor threads.");
		exit(3);
	}
}
