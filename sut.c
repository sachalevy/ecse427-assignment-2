#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

#include "sut.h"
#include "queue.h"

threadDescriptor thread_array[MAX_THREADS];
int thread_count, current_thread;
ucontext_t parent;

struct queue task_queue, io_queue, tid_queue;

// example for pthread at https://www.ibm.com/docs/en/zos/2.3.0?topic=functions-pthread-create-create-thread
void *task_executor(void *arg) {
	bool is_parent = true;
	printf("starting task executor");
	while (true) {
		// currently running task, to be queued in the back
		struct queue_entry *current_thread_ptr = queue_pop_head(&task_queue);
		printf("hey from ze eeuctor");
		if (current_thread_ptr && is_parent) {
			// switch from parent to task context
			swapcontext(&parent, &(thread_array[(*(int*)current_thread_ptr->data)].thread_context));
			queue_insert_tail(&task_queue, current_thread_ptr);
			is_parent = false;
		} else if (current_thread_ptr && !is_parent && thread_count > 1) {
			// switch from current to next task context
			struct queue_entry *next_thread_ptr = queue_peek_front(&task_queue);
			if (next_thread_ptr) {
				// if no next tasks, keep executing current task
				swapcontext(&(thread_array[(*(int*)current_thread_ptr->data)].thread_context), &(thread_array[(*(int*)next_thread_ptr->data)].thread_context));
			}
			queue_insert_tail(&task_queue, current_thread_ptr);
		} else {
			// ref for usage of nanosleep: https://stackoverflow.com/a/7684399
			nanosleep((const struct timespec[]){{0, 500000000L}}, NULL);
		}
	}
}

void *io_executor(void *arg) {
	/**char *io_ret;
	printf("thread() entered with argument '%s'\n", arg);
	if ((ret = (char*) malloc(20)) == NULL) {
		perror("malloc() error");
		exit(2);
	}
	strcpy(ret, "This is a test");

	pthread_exit(ret);*/
}


void sut_init() {
	thread_count = 0;
	current_thread = 0;

	task_queue = queue_create();
	queue_init(&task_queue);
	io_queue = queue_create();
	queue_init(&io_queue);
	tid_queue = queue_create();
	queue_init(&tid_queue);

	for (int i = 0; i < MAX_THREADS; i++) {
		int j = i;
		struct queue_entry *allocatable_tid = queue_new_node(&j);
		queue_insert_tail(&tid_queue, allocatable_tid);
	}

	pthread_t thid_task_exec, thid_io_exec;
	void *io_ret, *task_ret;

	printf("Initializing task and IO threads.");

	if (pthread_create(&thid_task_exec, NULL, task_executor, NULL) != 0) {
		perror("Error creating the task executor thread.");
		exit(1);
	}

	pthread_join(thid_task_exec, NULL);

	//if (pthread_create(&thid_io_exec, NULL, io_executor, NULL) != 0) {
	//	perror("Error creating the io executor thread.");
	//	exit(1);
	//}

	/*if ((pthread_join(thid_task_exec, &task_ret) != 0) || (pthread_join(thid_io_exec, &io_ret) != 0)) {
		perror("Error while joining executor threads.");
		exit(3);
	}*/
}

bool sut_create(sut_task_f sut_task) {
	printf("creating task");
	threadDescriptor *thread_descriptor;

	struct queue_entry *thread_id = queue_pop_head(&tid_queue);
	printf("got threa id %d",*(int*)thread_id->data );
	if (!thread_id) {
		printf("Error: active thread limit was reached.\n");
		return false;
	}

	// init thread descriptor for task
	thread_descriptor = &(thread_array[*(int*)thread_id->data]);
	getcontext(&(thread_descriptor->thread_context));
	thread_descriptor->thread_id = *(int*)thread_id->data;
	thread_descriptor->thread_stack = (char *)malloc(THREAD_STACK_SIZE);
	thread_descriptor->thread_context.uc_stack.ss_sp = thread_descriptor->thread_stack;
	thread_descriptor->thread_context.uc_stack.ss_size = THREAD_STACK_SIZE;
	thread_descriptor->thread_context.uc_link = 0;
	thread_descriptor->thread_context.uc_stack.ss_flags = 0;
	thread_descriptor->thread_func = sut_task;

	makecontext(&(thread_descriptor->thread_context), sut_task, 1, thread_descriptor);

	struct queue_entry *task_thread_id = queue_new_node(&thread_descriptor);
	queue_insert_tail(&task_queue, task_thread_id);

	thread_count++;

	return true;
}

/*Yield is occuring directly from the task at the top of the queue.*/
void sut_yield() {
	// pop task queue to get next thread, to be queued at the tail
	struct queue_entry *current_thread_ptr = queue_pop_head(&task_queue);
	// take a look at which thread id is next in line
	struct queue_entry *next_thread_ptr = queue_peek_front(&task_queue);

	// swap contexts between the two tasks
	// TODO: make sure the thread context are properly referenced
	swapcontext(&(thread_array[(*(int*)current_thread_ptr->data)].thread_context), &(thread_array[(*(int*)next_thread_ptr->data)].thread_context));

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
		swapcontext(&(thread_array[(*(int*)current_thread_ptr->data)].thread_context), &(thread_array[(*(int*)next_thread_ptr->data)].thread_context));
	} else {
		// no mo tasks, switch back to parent thread
		swapcontext(&(thread_array[(*(int*)current_thread_ptr->data)].thread_context), &parent);
	}

	// TODO: clear position in array


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
