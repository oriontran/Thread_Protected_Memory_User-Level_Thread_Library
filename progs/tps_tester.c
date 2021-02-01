#include <limits.h>
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#include "queue.h"
#include "thread.h"
#include "sem.h"
#include "tps.h"

static char msg1[TPS_SIZE] = "Hello world this is one long string that is going to change!\n";
static sem_t sem1, sem2;
void *latest_mmap_addr; 

#define TEST_ASSERT(assert)				\
do {									\
	printf("ASSERT: " #assert " ... ");	\
	if (assert) {						\
		printf("PASS\n");				\
	} else	{							\
		printf("FAIL\n");				\
		exit(1);						\
	}									\
} while(0)

void *__real_mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off);
void *__wrap_mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off) {
    latest_mmap_addr = __real_mmap(addr, len, prot, flags, fildes, off);
    return latest_mmap_addr;
}

void test_create_destroy(void)
{
	fprintf(stderr, "\n*** TEST tps_create and tps_destroy ***\n");
	TEST_ASSERT(tps_create() == 0);
    TEST_ASSERT(tps_destroy() == 0);
}

void test_invalid_offset_length(void) {
    fprintf(stderr, "\n*** TEST invalid offset = TPS_SIZE + 1 ***\n");
    char *buffer = malloc(TPS_SIZE + 1);
    tps_create();
    TEST_ASSERT(tps_read(TPS_SIZE + 1, 5, buffer) == -1);
    TEST_ASSERT(tps_write(TPS_SIZE + 1, 1, buffer) == -1);
    fprintf(stderr, "\n*** TEST invalid length = TPS_SIZE + 1 ***\n");
    TEST_ASSERT(tps_read(0, TPS_SIZE + 1, buffer) == -1);
    TEST_ASSERT(tps_write(0, TPS_SIZE + 1, buffer) == -1);
    free(buffer);
    tps_destroy();
}

void test_pre_creation(void) {
    fprintf(stderr, "\n*** TEST pre TPS creation read and write ***\n");
    char *buffer = malloc(TPS_SIZE);
    TEST_ASSERT(tps_read(TPS_SIZE, 0, buffer) == -1);
    TEST_ASSERT(tps_write(TPS_SIZE, 0, buffer) == -1);
    free(buffer);
}

void test_int(void) {
    tps_create();
    int data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    bool check[10] = {false};
    int *number = malloc(sizeof(int));
    int i;
    fprintf(stderr, "\n*** TEST TPS with integers ***\n");
    for (i = 0; i < (int)sizeof(data) / (int)sizeof(data[0]); i++) {
        tps_write(i * (int)sizeof(int), (int)sizeof(int), &data[i]);
    }
    for (i = 0; i < (int)sizeof(data) / (int)sizeof(data[0]); i++) {
        memset(number, 0, (int)sizeof(int));
        tps_read(i * (int)sizeof(int), (int)sizeof(int), number);
        if(!memcmp(&data[i], number, (int)sizeof(int)))
            check[i] = true;
    }
    for (int j = 0; j < (int)sizeof(check)/(int)sizeof(check[0]); j++) {
        if (check[j] == false) {
            fprintf(stderr, "testing TPS with integers failed\n");
            exit(1);
        }
    }
    fprintf(stderr, "testing TPS with integers succeeded\n");
    free(number);
    tps_destroy();
}

void test_multiple_rw(void) {
    fprintf(stderr, "\n*** TEST TPS with multiple R/W ***\n");
    char *buffer = malloc(TPS_SIZE);
    tps_create();

    tps_write(0, TPS_SIZE, msg1);
    memset(buffer, 0, TPS_SIZE);
    tps_read(0, TPS_SIZE, buffer);
    printf("pre write buffer: %s", buffer);

    char *world = "WORLD";
    tps_write(6, 5, world);
    char one[] = "ONE";
    tps_write(20, 3, one);

    memset(buffer, 0, TPS_SIZE);
    tps_read(0, TPS_SIZE, buffer);
    printf("post write buffer: %s", buffer);

    char *comparison = malloc(TPS_SIZE);
    memset(comparison, 0, TPS_SIZE);
    tps_read(6, 5, comparison);
    TEST_ASSERT(!memcmp(comparison, world, (int)strlen(world) - 1));
    memset(comparison, 0, TPS_SIZE);
    tps_read(20, 3, comparison);
    TEST_ASSERT(!memcmp(comparison, one, (int)strlen(one) - 1));
    fprintf(stderr, "^^^NOTE: comparison found using tps_read with same offsets as tps_write\n");

    free(buffer);
    tps_destroy();
}

void *thread2a(__attribute__((unused)) void *arg) {
    tps_create();
    tps_write(0, TPS_SIZE, msg1);
    char *world = "WORLD";
    tps_write(6, 5, world);
    char one[] = "ONE";
    tps_write(20, 3, one);

    char *start_contents_of_2a_TPS = malloc(TPS_SIZE);
    memset(start_contents_of_2a_TPS, 0, TPS_SIZE);
    tps_read(0, TPS_SIZE, start_contents_of_2a_TPS);
    printf("post write (thread2a): %s", start_contents_of_2a_TPS);

    sem_up(sem1);
    sem_down(sem2);
    
    char *end_contents_of_2a_TPS = malloc(TPS_SIZE);
    memset(end_contents_of_2a_TPS, 0, TPS_SIZE);
    tps_read(0, TPS_SIZE, end_contents_of_2a_TPS);
    printf("string is same (thread2a): %s", end_contents_of_2a_TPS);
    int length = (int)strlen(start_contents_of_2a_TPS);
    TEST_ASSERT(memcmp(start_contents_of_2a_TPS, end_contents_of_2a_TPS, length - 1) == 0);
    tps_destroy();
    free(start_contents_of_2a_TPS);
    free(end_contents_of_2a_TPS);
    return NULL;
}

void *thread1a(__attribute__((unused)) void *arg) {
    pthread_t tid;
    char *buffer = malloc(TPS_SIZE);
    pthread_create(&tid, NULL, thread2a, NULL);

    sem_down(sem1);

    tps_clone(tid);
    memset(buffer, 0, TPS_SIZE);
    tps_read(0, TPS_SIZE, buffer);
    printf("post clone/read (thread1a): %s", buffer);
    char going[] = "GOING";
    tps_write(44, 5, going);
    tps_read(0, TPS_SIZE, buffer);
    printf("post write (thread1a): %s", buffer);

    sem_up(sem2);
    
    pthread_join(tid, NULL);
    tps_destroy();
    free(buffer);
    return NULL;
}

void complex1(void) {
    pthread_t tid;
    fprintf(stderr, "\n*** TEST 2a creates TPS and writes, 1a clones 2a's TPS and writes, 2a is unchanged ***\n");

    sem1 = sem_create(0);
    sem2 = sem_create(0);

    pthread_create(&tid, NULL, thread1a, NULL);
    pthread_join(tid, NULL);
    sem_destroy(sem1);
    sem_destroy(sem2);
}

void *thread1b(__attribute__((unused)) void *arg) {
    tps_create();
    char *tps_addr = latest_mmap_addr;
    tps_addr[0] = 0;
    tps_destroy();
    return NULL;
}

void simple_protect(void) {
    pthread_t tid;
    fprintf(stderr, "\n*** TEST thread accesses own TPS illegally ***\n");
    pthread_create(&tid, NULL, thread1b, NULL);
    pthread_join(tid, NULL);
}

int main(void)
{
    tps_init(1);
    test_create_destroy();
    test_invalid_offset_length();
    test_pre_creation();
    test_int();
    test_multiple_rw();
    complex1();
    simple_protect();
	return 0;
}
