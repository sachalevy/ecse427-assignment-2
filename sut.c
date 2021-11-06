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

struct queue task_queue, io_queue, tid_queue;

void *task_executor(void *arg) {
	bool is_running = false;
	while (true) {
		struct queue_entry *current_thread_ptr = queue_pop_head(&task_queue);
		if (!is_running && current_thread_ptr) {
			printf("Found a thread to run\n");
			swapcontext(&parent,  &(thread_array[0].thread_context));
			queue_insert_tail(&task_queue, current_thread_ptr);
			is_running = true;
		} else {
			nanosleep((const struct timespec[]){{0, 500000000L}}, NULL);
		}
	}
}

// example for pthread at https://www.ibm.com/docs/en/zos/2.3.0?topic=functions-pthread-create-create-thread
void *_task_executor(void *arg) {
	bool is_parent = true;
	printf("Starting task executor.\n");

	while (true) {

		if (yield) {
			printf("Running a yield operation.\n");
			// currently running task, to be queued in the back
			struct queue_entry *current_thread_ptr = queue_pop_head(&task_queue);
			swapcontext(&parent,
				&((*(threadDescriptor*)current_thread_ptr->data).thread_context));
			queue_insert_tail(&task_queue, current_thread_ptr);

			if (current_thread_ptr && is_parent) {
				// switch from parent to task context
				printf("hello from here");
				swapcontext(&parent,
					&((*(threadDescriptor*)current_thread_ptr->data).thread_context));
				queue_insert_tail(&task_queue, current_thread_ptr);
				is_parent = false;
				printf("successfully switched to the task to be executed\n");
			} else if (current_thread_ptr && !is_parent && thread_count > 1) {
				printf("hello from there");
				// switch from current to next task context
				struct queue_entry *next_thread_ptr = queue_peek_front(&task_queue);
				if (next_thread_ptr) {
					// if no next tasks, keep executing current task
					swapcontext(&((*(threadDescriptor*)current_thread_ptr->data).thread_context),
						&((*(threadDescriptor*)next_thread_ptr->data).thread_context));
				}
				queue_insert_tail(&task_queue, current_thread_ptr);
			} else {
				printf("did not find anyone\n");
			}
		}
		else {
			// ref for usage of nanosleep: https://stackoverflow.com/a/7684399
			nanosleep((const struct timespec[]){{0, 500000000L}}, NULL);
		}
	}
}

/*
- init threading library (2 threads running for I/O and tasks)
(want to dynamically allocate threads from the thread array)
(handle switches between threads from the C-exec pthread)
- create a new task
(add thread descriptor to task queue)
(set signal on thread to handle expiry -> variable set directl when running sut_yield)
(sut_yield activates signal to running thread which makes it yield to the next thread)
*/


bool sut_init() {
	thread_count = 0;
	current_thread = 0;
	current_thread_id = 0;

	task_queue = queue_create();
	queue_init(&task_queue);
	io_queue = queue_create();
	queue_init(&io_queue);
	tid_queue = queue_create();
	queue_init(&tid_queue);

	// add one node for debugging
	//int i = 0;
	//struct queue_entry *allocatable_tid = queue_new_node(&i);
	//queue_insert_tail(&tid_queue, allocatable_tid);

	// organise distribution of thread ids
	/*
	struct queue_entry *allocatable_tid;
	for (int i = 0; i < MAX_THREADS; i++) {
		allocatable_tid = queue_new_node(&i);
		queue_insert_tail(&tid_queue, allocatable_tid);
		printf("Adding new thread id %d.\n", *(int*)allocatable_tid->data);
	}
	*/

	pthread_t thid_task_exec, thid_io_exec;
	void *io_ret, *task_ret;

	if (pthread_create(&thid_task_exec, NULL, task_executor, NULL) != 0) {
		perror("Error creating the task executor thread.\n");
		exit(1);
	}

	//pthread_join(thid_task_exec, NULL);

	//if (pthread_create(&thid_io_exec, NULL, io_executor, NULL) != 0) {
	//	perror("Error creating the io executor thread.");
	//	exit(1);
	//}

	/*if ((pthread_join(thid_task_exec, &task_ret) != 0) || (pthread_join(thid_io_exec, &io_ret) != 0)) {
		perror("Error while joining executor threads.");
		exit(3);
	}*/
	return true;
}

bool sut_create(sut_task_f sut_task) {
	/*struct queue_entry *thread_id_ptr = queue_pop_head(&tid_queue);
	if (!thread_id_ptr) {
		printf("Error: active thread limit was reached.\n");
		return false;
	}

	current_thread_id = (*(int*)thread_id_ptr->data);
	printf("Assigning thread id %d.\n", current_thread_id);
	*/
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
	queue_insert_tail(&task_queue, task_thread_id);

	if (thread_count == 0) {
		printf("first task to be queued so should yield to the task.");
		yield = 1;
	}

	thread_count++;

	return true;
}

/*Yield is occuring directly from the task at the top of the queue.*/
void sut_yield() {
	// >> send a pthread_kill signal s

	// pop task queue to get next thread, to be queued at the tail
	struct queue_entry *current_thread_ptr = queue_pop_head(&task_queue);
	// take a look at which thread id is next in line
	struct queue_entry *next_thread_ptr = queue_peek_front(&task_queue);

	// swap contexts between the two tasks
	// TODO: make sure the thread context are properly referenced
	swapcontext(&((*(threadDescriptor*)current_thread_ptr->data).thread_context),
		&((*(threadDescriptor*)next_thread_ptr->data).thread_context));

	// put back in queue both tasks - current going to tail
	queue_insert_tail(&task_queue, current_thread_ptr);
}

/*Remove the thread from the thread array, and from the queue,*/
void sut_exit() {
	struct queue_entry *current_thread_ptr = queue_pop_head(&task_queue);
	struct queue_entry *next_thread_ptr = queue_peek_front(&task_queue);

	// need to update the array - could maintain a queue of allocatable IDs?
	// here would need to destroy the stack of the current function and free this
	// thread id in order to make sure can re-create a new tasks

	// check if there is any next tasks following the current task in the queue
	if (next_thread_ptr) {
		// switch to another task
		swapcontext(&((*(threadDescriptor*)current_thread_ptr->data).thread_context),
			&((*(threadDescriptor*)next_thread_ptr->data).thread_context));
	} else {
		// no mo tasks, switch back to parent thread
		swapcontext(&((*(threadDescriptor*)current_thread_ptr->data).thread_context),
			&parent);
	}

	// add freed thread id back in allocatable thread id queue
	// NOTE: this works well for 32 thread but would not be efficient at
	// a larger scale (managing a queue is pretty costly)
	// TODO: make sure no memory leaked with thread id allocation process
	queue_insert_tail(&tid_queue, current_thread_ptr);

	thread_count--;
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


}
