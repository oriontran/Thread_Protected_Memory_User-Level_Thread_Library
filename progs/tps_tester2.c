#include <limits.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>

#include "queue.h"
#include "thread.h"
#include "sem.h"
#include "tps.h"

static sem_t sem1, sem2, sem3;
void *latest_mmap_addr; 
static char msg1[TPS_SIZE] = "hello world!\n";

void *__real_mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off);
void *__wrap_mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off) {
    latest_mmap_addr = __real_mmap(addr, len, prot, flags, fildes, off);
    return latest_mmap_addr;
}

void *thread3(__attribute__((unused)) void *arg) {
	char *buffer = malloc(TPS_SIZE);
	tps_create();
	tps_write(0, TPS_SIZE, msg1);
	memset(buffer, 0, TPS_SIZE);
	tps_read(0, TPS_SIZE, buffer);

	printf("thread3 initial string: %s", buffer);
	char *world = "WORLD";
    tps_write(6, 5, world);
    memset(buffer, 0, TPS_SIZE);
	tps_read(0, TPS_SIZE, buffer);
	printf("thread3 modified string: %s", buffer);

	sem_up(sem2);
	sem_down(sem3);

	printf("If this prints, then the protection failed\n");
	free(buffer);
	tps_destroy();
	return NULL;
}
void *thread2(__attribute__((unused)) void *arg) {
	pthread_t tid;
	char *buffer = malloc(TPS_SIZE);
    pthread_create(&tid, NULL, thread3, NULL);

    sem_down(sem2);

    tps_clone(tid);
    memset(buffer, 0, TPS_SIZE);
    tps_read(0, TPS_SIZE, buffer);
    printf("thread2 initial string = thread3 modified string: %s", buffer);
	char *hello = "HELLO";
    tps_write(0, 5, hello);
	memset(buffer, 0, TPS_SIZE);
    tps_read(0, TPS_SIZE, buffer);
	printf("thread2 modified string: %s", buffer);
    sem_up(sem1);

    free(buffer);
    tps_destroy();
    return NULL;
}
void *thread1(__attribute__((unused)) void *arg) {
	pthread_t tid;
	char *buffer = malloc(TPS_SIZE);
    pthread_create(&tid, NULL, thread2, NULL);
    sem_down(sem1);
    char *tps_addr = latest_mmap_addr;
    memcpy(buffer, tps_addr, TPS_SIZE);
    tps_addr[0] = 0;
    sem_up(sem3);
    tps_destroy();
    return NULL;
}

int main(void) {
	pthread_t tid;
    fprintf(stderr, "\n*** More complex TPS protection ***\n");
    tps_init(100);

    sem1 = sem_create(0);
    sem2 = sem_create(0);
    sem3 = sem_create(0);

    pthread_create(&tid, NULL, thread1, NULL);
    pthread_join(tid, NULL);

    sem_destroy(sem1);
    sem_destroy(sem2);
    sem_destroy(sem3);
}
