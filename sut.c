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
ucontext_t task_parent, io_parent;
pthread_t thid_task_exec, thid_io_exec;
struct queue task_queue, io_queue, tid_queue;
int exec_count = 0;
pthread_mutex_t thid_mutex, task_mutex, io_mutex;

sigset_t io_sigset, task_sigset;

void handle_yield() {
	/*Upon SIGUSR1 being triggered, take a look at current process in line.*/
	pthread_mutex_lock(&task_mutex);
	struct queue_entry *current_thread_ptr = queue_pop_head(&task_queue);
	printf("Caught a task yiekd on %d\n", *(int*)current_thread_ptr->data);
	pthread_mutex_unlock(&task_mutex);

	// can only ever yield user thread if there is one
	if (current_thread_ptr) {
		//printf("Inserting old current task to tail: %d\n", *(int*)current_thread_ptr->data);
		pthread_mutex_lock(&task_mutex);
		queue_insert_tail(&task_queue, current_thread_ptr);
		struct queue_entry *next_thread_ptr = queue_peek_front(&task_queue);
		pthread_mutex_unlock(&task_mutex);

		if (next_thread_ptr != NULL && (*(int*)current_thread_ptr->data) != (*(int*)next_thread_ptr->data)) {
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
	printf("supposed to exit\n");
	pthread_mutex_lock(&task_mutex);
	struct queue_entry *current_thread_ptr = queue_pop_head(&task_queue);
	struct queue_entry *alloc_thread_id = queue_new_node(&thread_ids[*(int*)current_thread_ptr->data]);
	queue_insert_tail(&tid_queue, alloc_thread_id);
	thread_count--;
	printf("hello\n");
	pthread_mutex_unlock(&task_mutex);

	if (current_thread_ptr) {
		pthread_mutex_lock(&task_mutex);
		struct queue_entry *next_thread_ptr = queue_peek_front(&task_queue);
		pthread_mutex_unlock(&task_mutex);

		if (next_thread_ptr) {
			pthread_mutex_lock(&task_mutex);
			next_thread_ptr = queue_pop_head(&task_queue);
			struct queue_entry *next_next_thread_ptr = queue_peek_front(&task_queue);
			pthread_mutex_unlock(&task_mutex);
			printf("future next thread upon exit of %d is %d and next next %d\n",*(int*)current_thread_ptr->data,  *(int*)next_thread_ptr->data, 1);
			swapcontext(&(thread_array[*(int*)current_thread_ptr->data].thread_context),
				&(thread_array[*(int*)next_thread_ptr->data].thread_context));
		} else {
			printf("nobody to exit to for %d\n", *(int*)current_thread_ptr->data);
			swapcontext(&(thread_array[*(int*)current_thread_ptr->data].thread_context),
				&task_parent);
		}
	} else {
		printf("no current thread ptr\n");
	}
}

void handle_shutdown() {
	bool shutdown_cond = true;
}

void handle_io() {
	pthread_mutex_lock(&task_mutex);
	struct queue_entry *current_thread_ptr = queue_pop_head(&task_queue);
	//printf("got a siganl on task thread about %d\n", *(int*)current_thread_ptr->data);
	pthread_mutex_unlock(&task_mutex);

	if (current_thread_ptr) {
		//printf("Inserting old current task to tail: %d\n", *(int*)current_thread_ptr->data);
		pthread_mutex_lock(&io_mutex);
		queue_insert_tail(&io_queue, current_thread_ptr);
		pthread_mutex_unlock(&io_mutex);
		pthread_mutex_lock(&task_mutex);
		struct queue_entry *next_thread_ptr = queue_peek_front(&task_queue);
		pthread_mutex_unlock(&task_mutex);

		if (next_thread_ptr) {
			//printf("found a valid next ptr so switching to it %d\n", *(int*)next_thread_ptr->data);
			swapcontext(&(thread_array[*(int*)current_thread_ptr->data].thread_context),
				&(thread_array[*(int*)next_thread_ptr->data].thread_context));
		} else {
			swapcontext(&(thread_array[*(int*)current_thread_ptr->data].thread_context),
				&task_parent);
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
		//printf("found current task\n");
		//printf("Inserting old current task to tail: %d\n", *(int*)current_thread_ptr->data);
		pthread_mutex_lock(&task_mutex);
		queue_insert_tail(&task_queue, current_thread_ptr);
		pthread_mutex_unlock(&task_mutex);
		pthread_mutex_lock(&io_mutex);
		struct queue_entry *next_thread_ptr = queue_peek_front(&io_queue);
		pthread_mutex_unlock(&io_mutex);
		
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

void *task_executor(void *arg) {
	struct sigaction yield_handler;
	yield_handler.sa_handler = handle_yield;
	sigaction(SIGUSR1, &yield_handler, NULL);

	struct sigaction io_handler;
	io_handler.sa_handler = handle_io;
	sigaction(SIGCHLD, &io_handler, NULL);

	struct sigaction exit_handler;
	exit_handler.sa_handler = handle_exit;
	sigaction(SIGUSR2, &exit_handler, NULL);

	int is_member = sigismember(&task_sigset, SIGUSR1);
	printf("is signal member %d\n", is_member);
	int did_unblock = pthread_sigmask(SIG_UNBLOCK, &task_sigset, NULL);
	
	while (true) {
		pthread_mutex_lock(&task_mutex);
		struct queue_entry *current_thread_ptr = queue_peek_front(&task_queue);
		pthread_mutex_unlock(&task_mutex);

		if (current_thread_ptr) {
			printf("Found something interesting %d\n", *(int*)current_thread_ptr->data);
			swapcontext(&task_parent,  &(thread_array[*(int*)current_thread_ptr->data].thread_context));
			printf("welcome back!\n");
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


void *io_executor(void *arg) {
	struct sigaction resume_handler;
	resume_handler.sa_handler = handle_resume;
	sigaction(SIGWINCH, &resume_handler, NULL);

	int did_unblock = pthread_sigmask(SIG_UNBLOCK, &io_sigset, NULL);

	while (true) {
		pthread_mutex_lock(&io_mutex);
		struct queue_entry *current_thread_ptr = queue_peek_front(&io_queue);
		pthread_mutex_unlock(&io_mutex);

		//printf("did find an io task to run %d\n", (current_thread_ptr==NULL));
		if (current_thread_ptr) {
			swapcontext(&io_parent, &(thread_array[*(int*)current_thread_ptr->data].thread_context));
		} else {
			nanosleep((const struct timespec[]){{0, 500000000L}}, NULL);
		}
	}
}

bool sut_init() {
	sigemptyset(&task_sigset);
	sigaddset(&task_sigset, SIGUSR2);
	sigaddset(&task_sigset, SIGUSR1);
	sigaddset(&task_sigset, SIGCHLD);
	pthread_sigmask(SIG_BLOCK, &task_sigset, NULL);
	
	sigemptyset(&io_sigset);
	sigaddset(&io_sigset, SIGWINCH);
	pthread_sigmask(SIG_BLOCK, &io_sigset, NULL);

	//sigset_t new;
	//sigfillset(&new);
	//pthread_sigmask(SIG_SETMASK, &new, NULL);

	if (pthread_mutex_init(&io_mutex, NULL) != 0 || pthread_mutex_init(&thid_mutex, NULL) != 0 || pthread_mutex_init(&task_mutex, NULL) != 0) {
		printf("Failed to init one or more mutex locks.\n");
		exit(1);
	}

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

	printf("io exec %ld task exec %ld\n", thid_io_exec, thid_task_exec);

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
	thread_descriptor->thread_id = thread_count;
	thread_descriptor->thread_stack = (char *)malloc(THREAD_STACK_SIZE);
	thread_descriptor->thread_context.uc_stack.ss_sp = thread_descriptor->thread_stack;
	thread_descriptor->thread_context.uc_stack.ss_size = THREAD_STACK_SIZE;
	thread_descriptor->thread_context.uc_link = 0;
	thread_descriptor->thread_context.uc_stack.ss_flags = 0;
	thread_descriptor->thread_func = sut_task;
	makecontext(&(thread_descriptor->thread_context), sut_task, 1, thread_descriptor);

	pthread_mutex_lock(&task_mutex);
	struct queue_entry *task_thread_id = queue_new_node(&(thread_descriptor->thread_id));
	queue_insert_tail(&task_queue, task_thread_id);
	thread_count++;
	struct queue_entry *n = queue_peek_front(&task_queue);
	printf("current peek of queue %d\n", *(int*)n->data);
	pthread_mutex_unlock(&task_mutex);

	//printf("Inserting new function to be executed in task queue.\n");

	return true;
}

void sut_yield() {
	pthread_kill(thid_task_exec, SIGUSR1);
}

void sut_exit() {
	pthread_kill(thid_task_exec, SIGUSR2);
}

int sut_open(char *dest) {
	pthread_kill(thid_task_exec, SIGCHLD);
	int fd;
	// see https://linux.die.net/man/3/open
	fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	printf("hey opening %d\n", fd);
	pthread_kill(thid_io_exec, SIGWINCH);

	return fd;
}

void sut_write(int fd, char *buf, int size) {
	pthread_kill(thid_task_exec, SIGCHLD);
	write(fd, buf, size);
	pthread_kill(thid_io_exec, SIGWINCH);
}

void sut_close(int fd) {
	pthread_kill(thid_task_exec, SIGCHLD);
	close(fd);
	pthread_kill(thid_io_exec, SIGWINCH);
}

char *sut_read(int fd, char *buf, int size) {
	pthread_kill(thid_task_exec, SIGCHLD);
	ssize_t rfd;
	rfd = read(fd, buf, size);
	pthread_kill(thid_io_exec, SIGWINCH);

	if (rfd == -1) {
		return NULL;
	} else {
		return (char*)rfd;
	}
}

void sut_shutdown() {
	//pthread_kill(thid_task_exec, SIGINT);

	void *task_retval, *io_retval;
	if ((pthread_join(thid_task_exec, &task_retval) != 0) && (pthread_join(thid_io_exec, &io_retval) != 0)) {
		perror("Error while joining executor threads.");
		exit(3);
	}
}
