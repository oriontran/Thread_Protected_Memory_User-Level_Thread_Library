#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#include "queue.h"
#include "sem.h"
#include "thread.h"

struct semaphore {
	int count;
	queue_t waiting_threads;
	pthread_t *recent_unblock;
};

sem_t sem_create(size_t count)
{
	enter_critical_section();
	sem_t sem = calloc(1, sizeof(struct semaphore));
	sem->count = count;
	sem->waiting_threads = queue_create();
	sem->recent_unblock = NULL;
	exit_critical_section();
	if (!sem)
		return NULL;
	return sem;
}

int sem_destroy(sem_t sem)
{
	if (!sem || queue_length(sem->waiting_threads) > 0)
		return -1;
	free(sem);
	sem = NULL;
	return 0;
}

int sem_down(sem_t sem)
{
	if (!sem)
		return -1;

	enter_critical_section();
	if (sem->count == 0) {
		pthread_t self = pthread_self();
		queue_enqueue(sem->waiting_threads, &self);
		thread_block();
	}
	if (sem->count == 0 && sem->recent_unblock != NULL) {
		pthread_t self = pthread_self();
		if (*(sem->recent_unblock) == self) {
			queue_enqueue(sem->waiting_threads, &self);
			sem->recent_unblock = NULL;
			thread_block();
		}
	}
	sem->count -= 1;
	exit_critical_section();

	return 0;
}

int sem_up(sem_t sem)
{
	pthread_t *from_queue; 
	if (!sem)
		return -1;
	enter_critical_section();
	sem->count += 1;
	if (queue_length(sem->waiting_threads) > 0) {
		queue_dequeue(sem->waiting_threads, (void**)&from_queue);
		thread_unblock(*from_queue);
		sem->recent_unblock = from_queue;
	}
	exit_critical_section();
	
	return 0;
}

int sem_getvalue(sem_t sem, int *sval)
{
	if (!sem || !sval)
		return -1;
	if (sem->count > 0) {
		*sval = sem->count;
	} else {
		*sval = -queue_length(sem->waiting_threads);
	}
	return 0;
}
