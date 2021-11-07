#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/stat.h> 

#include "sut.h"
#include "queue.h"

int thread_ids[MAX_THREADS];
threadDescriptor thread_array[MAX_THREADS];
int thread_count;
ucontext_t parent;
pthread_t thid_task_exec, thid_io_exec;
struct queue task_queue, io_queue, tid_queue;
int exec_count = 0;
bool shutdown_cond = false;

void handle_yield() {
	/*Upon SIGUSR1 being triggered, take a look at current process in line.*/
	struct queue_entry *current_thread_ptr = queue_pop_head(&task_queue);

	//printf("Caught a task yiekd on %d\n", *(int*)current_thread_ptr->data);

	// can only ever yield user thread if there is one
	if (current_thread_ptr) {
		//printf("Inserting old current task to tail: %d\n", *(int*)current_thread_ptr->data);
		queue_insert_tail(&task_queue, current_thread_ptr);
		struct queue_entry *next_thread_ptr = queue_peek_front(&task_queue);
		if (next_thread_ptr) {
			//printf("found a valid next ptr so switching to it %d\n", *(int*)next_thread_ptr->data);
			swapcontext(&(thread_array[*(int*)current_thread_ptr->data].thread_context),
				&(thread_array[*(int*)next_thread_ptr->data].thread_context));
		}
		
	}
	// check that the total executions adds up to the tests
	exec_count++;
}

void handle_exit() {
	/*Upon SIGUSR2 being triggered, stop current task and remove it from queue.*/
	struct queue_entry *current_thread_ptr = queue_pop_head(&task_queue);
	struct queue_entry *alloc_thread_id = queue_new_node(&thread_ids[*(int*)current_thread_ptr->data]);
	queue_insert_tail(&tid_queue, alloc_thread_id);
	thread_count--;

	//printf("caught a sig exit total execs %d\n", exec_count);
	
	// can only ever exit user thread if there is one
	if (current_thread_ptr) {
		struct queue_entry *next_thread_ptr = queue_peek_front(&task_queue);
		if (next_thread_ptr) {
			swapcontext(&(thread_array[*(int*)current_thread_ptr->data].thread_context),
				&(thread_array[*(int*)next_thread_ptr->data].thread_context));
		} else {
			swapcontext(&(thread_array[*(int*)current_thread_ptr->data].thread_context),
				&parent);
		}
	}
}

void handle_shutdown() {
	shutdown_cond = true;
}

void handle_io() {
	struct queue_entry *current_thread_ptr = queue_pop_head(&task_queue);

	if (current_thread_ptr) {
		//printf("Inserting old current task to tail: %d\n", *(int*)current_thread_ptr->data);
		queue_insert_tail(&io_queue, current_thread_ptr);
		struct queue_entry *next_thread_ptr = queue_peek_front(&task_queue);
		if (next_thread_ptr) {
			//printf("found a valid next ptr so switching to it %d\n", *(int*)next_thread_ptr->data);
			swapcontext(&(thread_array[*(int*)current_thread_ptr->data].thread_context),
				&(thread_array[*(int*)next_thread_ptr->data].thread_context));
		} else {
			swapcontext(&(thread_array[*(int*)current_thread_ptr->data].thread_context),
				&parent);
		}
	}
}

void *task_executor(void *arg) {
	struct sigaction yield_handler;
	yield_handler.sa_handler = handle_yield;
	sigaction(SIGUSR1, &yield_handler, NULL);

	struct sigaction exit_handler;
	exit_handler.sa_handler = handle_exit;
	sigaction(SIGUSR2, &exit_handler, NULL);

	struct sigaction io_handler;
	io_handler.sa_handler = handle_io;
	sigaction(SIGWINCH, &io_handler, NULL);

	//struct sigaction shutdown_handler;
	//shutdown_handler.sa_handler = handle_shutdown;
	//sigaction(SIGINT, &shutdown_handler, NULL);
	
	bool is_running = false;
	while (true) {
		struct queue_entry *current_thread_ptr = queue_peek_front(&task_queue);
		//printf("Found something interesting %d\n", !is_running && current_thread_ptr);
		if (!is_running && current_thread_ptr) {
			swapcontext(&parent,  &(thread_array[*(int*)current_thread_ptr->data].thread_context));
			is_running = true;
		} else {
			//printf("Sleeping for a bit...\n");
			nanosleep((const struct timespec[]){{0, 500000000L}}, NULL);
		}

		//current_thread_ptr = queue_peek_front(&task_queue);
		//if (shutdown_cond && !current_thread_ptr && thread_count == 0) {
		//	break;
		//}
	}
}

void handle_resume() {
	struct queue_entry *current_thread_ptr = queue_pop_head(&io_queue);

	if (current_thread_ptr) {
		//printf("Inserting old current task to tail: %d\n", *(int*)current_thread_ptr->data);
		queue_insert_tail(&task_queue, current_thread_ptr);
		struct queue_entry *next_thread_ptr = queue_peek_front(&io_queue);
		if (next_thread_ptr) {
			//printf("found a valid next ptr so switching to it %d\n", *(int*)next_thread_ptr->data);
			swapcontext(&(thread_array[*(int*)current_thread_ptr->data].thread_context),
				&(thread_array[*(int*)next_thread_ptr->data].thread_context));
		} else {
			swapcontext(&(thread_array[*(int*)current_thread_ptr->data].thread_context),
				&parent);
		}
	}
}

void *io_executor(void *arg) {
	struct sigaction resume_handler;
	resume_handler.sa_handler = handle_resume;
	sigaction(SIGWINCH, &resume_handler, NULL);

	bool is_running = false;
	while (true) {
		struct queue_entry *current_thread_ptr = queue_peek_front(&io_queue);
		if (!is_running && current_thread_ptr) {
			swapcontext(&parent, &(thread_array[*(int*)current_thread_ptr->data].thread_context));
			is_running = true;
		} else {
			nanosleep((const struct timespec[]){{0, 500000000L}}, NULL);
		}
	}
}

bool sut_init() {
	thread_count = 0;

	tid_queue = queue_create();
	queue_init(&tid_queue);
	for (int i = 0; i < MAX_THREADS; i++) {
		thread_ids[i] = i;
		struct queue_entry *allocatable_tid = queue_new_node(&thread_ids[i]);
		queue_insert_tail(&tid_queue, allocatable_tid);
	}

	task_queue = queue_create();
	queue_init(&task_queue);

	io_queue = queue_create();
	queue_init(&io_queue);

	if (pthread_create(&thid_task_exec, NULL, task_executor, NULL) != 0) {
		perror("Error creating the task executor thread.\n");
		exit(1);
	}

	if (pthread_create(&thid_io_exec, NULL, io_executor, NULL) != 0) {
		perror("Error creating the io executor thread.\n");
		exit(1);
	}

	return true;
}

bool sut_create(sut_task_f sut_task) {
	struct queue_entry *alloc_thread_id = queue_pop_head(&tid_queue);
	printf("got thread id %d\n", *(int*)alloc_thread_id->data);
	if (!alloc_thread_id) {
		return false;
	}

	threadDescriptor *thread_descriptor;
	thread_descriptor = &(thread_array[*(int*)alloc_thread_id->data]);
	getcontext(&(thread_descriptor->thread_context));
	thread_descriptor->thread_id = thread_count;
	thread_descriptor->thread_stack = (char *)malloc(THREAD_STACK_SIZE);
	thread_descriptor->thread_context.uc_stack.ss_sp = thread_descriptor->thread_stack;
	thread_descriptor->thread_context.uc_stack.ss_size = THREAD_STACK_SIZE;
	thread_descriptor->thread_context.uc_link = 0;
	thread_descriptor->thread_context.uc_stack.ss_flags = 0;
	thread_descriptor->thread_func = sut_task;
	makecontext(&(thread_descriptor->thread_context), sut_task, 1, thread_descriptor);

	//printf("Inserting new function to be executed in task queue.\n");
	struct queue_entry *task_thread_id = queue_new_node(&(thread_descriptor->thread_id));
	queue_insert_tail(&task_queue, task_thread_id);
	thread_count++;

	return true;
}

void sut_yield() {
	pthread_kill(thid_task_exec, SIGUSR1);
}

void sut_exit() {
	pthread_kill(thid_task_exec, SIGUSR2);
}

int sut_open(char *dest) {
	pthread_kill(thid_task_exec, SIGWINCH);
	int fd;
	// see https://linux.die.net/man/3/open
	fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
	pthread_kill(io_task_exec, SIGWINCH);

	return fd;
}

void sut_write(int fd, char *buf, int size) {
	pthread_kill(thid_task_exec, SIGWINCH);


	pthread_kill(io_task_exec, SIGWINCH);
}

void sut_close(int fd) {
	pthread_kill(thid_task_exec, SIGWINCH);


	pthread_kill(io_task_exec, SIGWINCH);


}

char *sut_read(int fd, char *buf, int size) {
	pthread_kill(thid_task_exec, SIGWINCH);


	pthread_kill(io_task_exec, SIGWINCH);

}

void sut_shutdown() {
	//pthread_kill(thid_task_exec, SIGINT);

	void *retval;
	if (pthread_join(thid_task_exec, &retval) != 0) {
		perror("Error while joining executor threads.");
		exit(3);
	}
}
