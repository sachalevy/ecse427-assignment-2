#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/stat.h> 
#include <fcntl.h>

#include "sut.h"
#include "queue.h"

int thread_ids[MAX_THREADS];
threadDescriptor thread_array[MAX_THREADS];
int thread_count;
ucontext_t io_parent;
pthread_t thid_io_exec;
ucontext_t task_parent[C_EXECS_COUNT];
pthread_t thid_task_exec[C_EXECS_COUNT];
struct queue wait_queue, io_queue, tid_queue;
struct queue_entry *running_threads[C_EXECS_COUNT];
pthread_mutex_t thid_mutex, task_mutex, io_mutex, file_mutex, shutdown_mutex;
bool do_shutdown;

int find_ctx_idx() {
	pthread_t current_thid = pthread_self();
	if (C_EXECS_COUNT == 1) {
		return 0;
	} else if (current_thid == thid_task_exec[0]) {
		return 0;
	} else {
		return 1;
	}
}

void handle_yield() {
	int ctx_idx;
	pthread_mutex_lock(&task_mutex);
	struct queue_entry *current_thread_ptr = queue_pop_head(&wait_queue);
	//printf("Caught a task yiekd on %d\n", *(int*)current_thread_ptr->data);
	pthread_mutex_unlock(&task_mutex);

	if (current_thread_ptr) {
		//printf("Inserting old current task to tail: %d\n", *(int*)current_thread_ptr->data);
		pthread_mutex_lock(&task_mutex);
		queue_insert_tail(&wait_queue, current_thread_ptr);
		struct queue_entry *next_thread_ptr = queue_peek_front(&wait_queue);
		pthread_mutex_unlock(&task_mutex);

		if (next_thread_ptr != NULL && (*(int*)current_thread_ptr->data) != (*(int*)next_thread_ptr->data)) {
			//printf("found a valid next ptr so switching to it %d\n", *(int*)next_thread_ptr->data);
			swapcontext(&(thread_array[*(int*)current_thread_ptr->data].thread_context),
				&(thread_array[*(int*)next_thread_ptr->data].thread_context));
		} else {
			ctx_idx = find_ctx_idx();
			printf("retrieved ctx idx %d\n", ctx_idx);
			
			swapcontext(&(thread_array[*(int*)current_thread_ptr->data].thread_context),
				&task_parent[ctx_idx]);
		}
		
	}
}

void handle_exit() {
	pthread_mutex_lock(&task_mutex);
	struct queue_entry *current_thread_ptr = queue_pop_head(&wait_queue);
	struct queue_entry *alloc_thread_id = queue_new_node(&thread_ids[*(int*)current_thread_ptr->data]);
	queue_insert_tail(&tid_queue, alloc_thread_id);
	thread_count--;
	pthread_mutex_unlock(&task_mutex);

	if (current_thread_ptr) {
		pthread_mutex_lock(&task_mutex);
		struct queue_entry *next_thread_ptr = queue_peek_front(&wait_queue);
		pthread_mutex_unlock(&task_mutex);

		if (next_thread_ptr) {
			pthread_mutex_lock(&task_mutex);
			next_thread_ptr = queue_pop_head(&wait_queue);
			struct queue_entry *next_next_thread_ptr = queue_peek_front(&wait_queue);
			queue_insert_head(&wait_queue, next_thread_ptr);
			pthread_mutex_unlock(&task_mutex);
			printf("future next thread upon exit of %d is %d and next next %d\n",*(int*)current_thread_ptr->data,  *(int*)next_thread_ptr->data, (next_next_thread_ptr != NULL));
			swapcontext(&(thread_array[*(int*)current_thread_ptr->data].thread_context),
				&(thread_array[*(int*)next_thread_ptr->data].thread_context));
		} else {
			printf("nobody to exit to for %d\n", *(int*)current_thread_ptr->data);


			int ctx_idx = find_ctx_idx();
			printf("retrieved ctx idx %d\n", ctx_idx);
			swapcontext(&(thread_array[*(int*)current_thread_ptr->data].thread_context),
				&task_parent[ctx_idx]);
		}
	} else {
		printf("no current thread ptr\n");
	}
}

void handle_io() {
	int ctx_idx = find_ctx_idx();
	
	pthread_mutex_lock(&task_mutex);
	struct queue_entry *current_thread_ptr = queue_pop_head(&wait_queue);
	//printf("got a siganl on task thread about %d\n", *(int*)current_thread_ptr->data);
	pthread_mutex_unlock(&task_mutex);

	if (running_threads[ctx_idx]) {
		//printf("Inserting old current task to tail: %d\n", *(int*)current_thread_ptr->data);
		pthread_mutex_lock(&task_mutex);
		struct queue_entry *next_thread_ptr = queue_peek_front(&wait_queue);
		pthread_mutex_unlock(&task_mutex);

		pthread_mutex_lock(&io_mutex);
		queue_insert_tail(&io_queue, current_thread_ptr);
		pthread_mutex_unlock(&io_mutex);
		
		if (next_thread_ptr) {
			//printf("found a valid next ptr so switching to it %d\n", *(int*)next_thread_ptr->data);
			swapcontext(&(thread_array[*(int*)current_thread_ptr->data].thread_context),
				&(thread_array[*(int*)next_thread_ptr->data].thread_context));
		} else {
			swapcontext(&(thread_array[*(int*)current_thread_ptr->data].thread_context),
				&task_parent[ctx_idx]);
		}
	} else {
		printf("Got an IO call but could not find any ongoing task.\n");
	}
}


void handle_resume() {
	pthread_mutex_lock(&io_mutex);
	struct queue_entry *current_thread_ptr = queue_pop_head(&io_queue);
	pthread_mutex_unlock(&io_mutex);

	//printf("asking to resume %d\n", *(int*)current_thread_ptr->data);

	if (current_thread_ptr) {
		//printf("Inserting old current task to tail: %d\n", *(int*)current_thread_ptr->data);

		pthread_mutex_lock(&io_mutex);
		struct queue_entry *next_thread_ptr = queue_peek_front(&io_queue);
		pthread_mutex_unlock(&io_mutex);

		pthread_mutex_lock(&task_mutex);
		queue_insert_tail(&wait_queue, current_thread_ptr);
		pthread_mutex_unlock(&task_mutex);
		
		if (next_thread_ptr) {
			//printf("found a valid next ptr so switching to it %d\n", *(int*)next_thread_ptr->data);
			swapcontext(&(thread_array[*(int*)current_thread_ptr->data].thread_context),
				&(thread_array[*(int*)next_thread_ptr->data].thread_context));
		} else {
			//printf("no task found switching back to io parent\n");
			swapcontext(&(thread_array[*(int*)current_thread_ptr->data].thread_context),
				&io_parent);
		}
	}
}

bool check_shutdown() {
	bool do_break;
	struct queue_entry *task_ptr, *io_ptr, *running_ptr;

	pthread_mutex_lock(&task_mutex);
	task_ptr = queue_peek_front(&wait_queue);
	bool none_running = true;
	for (int i = 0; i < C_EXECS_COUNT; i++) {
		if (running_threads[i] != NULL) {
			none_running = false;
			break;
		}
	}
	pthread_mutex_unlock(&task_mutex);

	pthread_mutex_lock(&io_mutex);
	io_ptr = queue_peek_front(&io_queue);
	pthread_mutex_unlock(&io_mutex);

	pthread_mutex_lock(&shutdown_mutex);
	do_break = (do_shutdown && task_ptr == NULL && io_ptr == NULL && none_running);
	pthread_mutex_unlock(&shutdown_mutex);

	return do_break;
}

void *task_executor(void *arg) {
	bool do_break;
	int ctx_idx = find_ctx_idx();
	struct queue_entry *task_ptr;
	while (true) {
		pthread_mutex_lock(&task_mutex);
		task_ptr = queue_pop_head(&wait_queue);
		pthread_mutex_unlock(&task_mutex);

		if (task_ptr) {
			running_threads[ctx_idx] = task_ptr;
			//printf("Found something interesting %d\n", *(int*)current_thread_ptr->data);
			swapcontext(&task_parent[ctx_idx],  &(thread_array[*(int*)task_ptr->data].thread_context));
		} else {
			//printf("Sleeping for a bit...\n");
			nanosleep((const struct timespec[]){{0, 500000000L}}, NULL);
		}

		
		do_break = check_shutdown();
		if (do_break) {
			break;
		}
	}
}

void *io_executor(void *arg) {
	struct queue_entry *io_ptr;
	bool do_break;
	while (true) {
		pthread_mutex_lock(&io_mutex);
		io_ptr = queue_peek_front(&io_queue);
		pthread_mutex_unlock(&io_mutex);

		//printf("did find an io task to run %d\n", (current_thread_ptr==NULL));
		if (io_ptr) {
			swapcontext(&io_parent, &(thread_array[*(int*)io_ptr->data].thread_context));
		} else {
			nanosleep((const struct timespec[]){{0, 500000000L}}, NULL);
		}

		do_break = check_shutdown();
		if (do_break) {
			break;
		}
	}
}

bool sut_init() {
	if (pthread_mutex_init(&io_mutex, NULL) != 0 || pthread_mutex_init(&thid_mutex, NULL) != 0 || pthread_mutex_init(&task_mutex, NULL) != 0 || pthread_mutex_init(&file_mutex, NULL) != 0) {
		printf("Failed to init one or more mutex locks.\n");
		exit(1);
	}

	thread_count = 0;
	do_shutdown = false;

	tid_queue = queue_create();
	queue_init(&tid_queue);
	for (int i = 0; i < MAX_THREADS; i++) {
		thread_ids[i] = i;
		struct queue_entry *allocatable_tid = queue_new_node(&thread_ids[i]);
		queue_insert_tail(&tid_queue, allocatable_tid);
	}

	wait_queue = queue_create();
	queue_init(&wait_queue);

	io_queue = queue_create();
	queue_init(&io_queue);

	for (int j = 0; j < C_EXECS_COUNT; j++) {
		if (pthread_create(&thid_task_exec[j], NULL, task_executor, NULL) != 0) {
			perror("Error creating the task executor thread.\n");
			exit(1);
		}
	}

	if (pthread_create(&thid_io_exec, NULL, io_executor, NULL) != 0) {
		perror("Error creating the io executor thread.\n");
		exit(1);
	}

	return true;
}

bool sut_create(sut_task_f sut_task) {
	pthread_mutex_lock(&thid_mutex);
	struct queue_entry *alloc_thread_id = queue_pop_head(&tid_queue);
	pthread_mutex_unlock(&thid_mutex);
	printf("creating new task with id %d\n", *(int*)alloc_thread_id->data );

	//printf("got thread id %d\n", *(int*)alloc_thread_id->data);
	if (!alloc_thread_id) {
		return false;
	}

	threadDescriptor *thread_descriptor;
	thread_descriptor = &(thread_array[*(int*)alloc_thread_id->data]);
	getcontext(&(thread_descriptor->thread_context));
	thread_descriptor->thread_id = *(int*)alloc_thread_id->data;
	thread_descriptor->thread_stack = (char *)malloc(THREAD_STACK_SIZE);
	thread_descriptor->thread_context.uc_stack.ss_sp = thread_descriptor->thread_stack;
	thread_descriptor->thread_context.uc_stack.ss_size = THREAD_STACK_SIZE;
	thread_descriptor->thread_context.uc_link = 0;
	thread_descriptor->thread_context.uc_stack.ss_flags = 0;
	thread_descriptor->thread_func = sut_task;
	makecontext(&(thread_descriptor->thread_context), sut_task, 1, thread_descriptor);

	pthread_mutex_lock(&task_mutex);
	struct queue_entry *task_thread_id = queue_new_node(&(thread_descriptor->thread_id));
	queue_insert_tail(&wait_queue, task_thread_id);
	thread_count++;
	pthread_mutex_unlock(&task_mutex);

	//printf("Inserting new function to be executed in task queue.\n");

	return true;
}

void sut_yield() {
	handle_yield();
}

void sut_exit() {
	handle_exit();
}

int sut_open(char *dest) {
	handle_io();
	pthread_mutex_lock(&file_mutex);
	int fd = open(dest, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	pthread_mutex_unlock(&file_mutex);
	handle_resume();

	return fd;
}

void sut_write(int fd, char *buf, int size) {
	handle_io();
	pthread_mutex_lock(&file_mutex);
	write(fd, buf, size);
	pthread_mutex_unlock(&file_mutex);
	handle_resume();
}

void sut_close(int fd) {
	handle_io();
	pthread_mutex_lock(&file_mutex);
	close(fd);
	pthread_mutex_unlock(&file_mutex);
	handle_resume();
}

char *sut_read(int fd, char *buf, int size) {
	handle_io();
	pthread_mutex_lock(&file_mutex);
	ssize_t rfd = read(fd, buf, size);
	pthread_mutex_unlock(&file_mutex);
	handle_resume();
	
	if (rfd == -1) {
		return NULL;
	} else {
		return (char*)rfd;
	}
}

void sut_shutdown() {
	pthread_mutex_lock(&shutdown_mutex);
	do_shutdown = true;
	pthread_mutex_unlock(&shutdown_mutex);

	void *task_retval, *io_retval;

	for (int i = 0; i < C_EXECS_COUNT; i++) {
		if (pthread_join(thid_task_exec[i], &task_retval) != 0) {
			printf("error terminating task exec %d\n", i);
		}
	}

	if (pthread_join(thid_io_exec, &io_retval) != 0) {
		printf("error terminating io exec\n");
	}
}
