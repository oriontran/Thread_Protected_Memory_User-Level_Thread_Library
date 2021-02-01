#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "queue.h"
#include "thread.h"
#include "tps.h"

queue_t thread_to_tps;
int init;

struct mem_page{
    void* page_location;
    int reference_counter;
};

typedef struct mem_page mem_page;
struct tps {
    pthread_t thread_id;
    mem_page* page_indirection;

};
typedef struct tps tps;

static int find_matching_thread (void *data, void *arg){
    tps* temp_tps = (tps*)data;
    pthread_t thread_id = *(pthread_t *)arg;
    if (temp_tps->thread_id == thread_id)
        return 1;
    return 0;
}

static int find_matching_page (void *data, void *arg){
    tps* temp_tps = (tps*)data;
    void* thread_page = arg;
    if (temp_tps->page_indirection->page_location == thread_page)
        return 1;
    return 0;
}

static void segv_handler(int sig, siginfo_t *si, void *context)
{
    void* idk = context;
    int idk1 = 1;
    if(idk){
        idk1+=1;
    }
    void *p_fault = (void*)((uintptr_t)si->si_addr & ~(TPS_SIZE - 1));
    tps* reference_tps = NULL;
    queue_iterate(thread_to_tps, find_matching_page, (void*)p_fault, (void**)&reference_tps);
    if (reference_tps != NULL)
        fprintf(stderr, "TPS protection error!\n");
    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    raise(sig);
}

int tps_init(int segv)
{
    enter_critical_section();
    if(init == 0) {
        thread_to_tps = queue_create();
        if (segv) {
            struct sigaction sa;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = SA_SIGINFO;
            sa.sa_sigaction = segv_handler;
            sigaction(SIGBUS, &sa, NULL);
            sigaction(SIGSEGV, &sa, NULL);
        }
        exit_critical_section();
        return 0;
    }
    exit_critical_section();
    return -1;
}

int tps_create(void)
{
    enter_critical_section();
    tps* new_tps = calloc(1, sizeof(tps));
    mem_page* new_mem_page = calloc(1, sizeof(mem_page));

    // errors
    if (!new_mem_page || !new_tps)
        return -1;
    tps* reference_tps = NULL;
    pthread_t curr_thread = pthread_self();
    queue_iterate(thread_to_tps, find_matching_thread, (void*)&curr_thread, (void**)&reference_tps);
    if(reference_tps != NULL)
        return -1;

    new_mem_page->page_location = mmap(NULL, TPS_SIZE, PROT_NONE, MAP_PRIVATE|MAP_ANON, -1, 0);
    new_mem_page->reference_counter = 1;
    new_tps->thread_id = pthread_self();
    new_tps->page_indirection = new_mem_page;
    queue_enqueue(thread_to_tps, new_tps);
    exit_critical_section();
    return 0;
}

int tps_destroy(void)
{
    enter_critical_section();
    tps* reference_tps = NULL;
    pthread_t curr_thread = pthread_self();
    queue_iterate(thread_to_tps, find_matching_thread, (void*)&curr_thread, (void**)&reference_tps);
    if(reference_tps != NULL) {
        queue_delete(thread_to_tps, (void*)reference_tps);
        free(reference_tps->page_indirection);
        reference_tps->page_indirection = NULL;
        free(reference_tps);
        reference_tps = NULL;
        exit_critical_section();
        return 0;
    } else {
        exit_critical_section();
        return -1;
    }
}

int tps_read(size_t offset, size_t length, void *buffer)
{
    if (((int)offset + (int)length) > TPS_SIZE || !buffer)
        return -1;
    enter_critical_section();
    tps* reference_tps = NULL;
    pthread_t curr_thread = pthread_self();
    queue_iterate(thread_to_tps, find_matching_thread, (void*)&curr_thread, (void**)&reference_tps);
    if(reference_tps != NULL) { // We have found a matching thread
        mprotect(reference_tps->page_indirection->page_location, TPS_SIZE, PROT_READ);
        memcpy(buffer, reference_tps->page_indirection->page_location + offset, length);
        mprotect(reference_tps->page_indirection->page_location, TPS_SIZE, PROT_NONE);
        exit_critical_section();
        return 0;
    }
    else {
        exit_critical_section();
    }
    return -1;
}

int tps_write(size_t offset, size_t length, void *buffer)
{
    if (((int)offset + (int)length) > TPS_SIZE || !buffer)
        return -1;
    enter_critical_section();
    tps* reference_tps = NULL;
    pthread_t curr_thread = pthread_self(); // curr thread is T1
    queue_iterate(thread_to_tps, find_matching_thread, (void*)&curr_thread, (void**)&reference_tps);
    if(reference_tps != NULL) { // We have found a matching thread
        queue_delete(thread_to_tps, reference_tps);
        if(reference_tps->page_indirection->reference_counter > 1){
            reference_tps->page_indirection->reference_counter--; // theres only 1 page
            mem_page* new_mem_page = calloc(1, sizeof(mem_page));
            new_mem_page->page_location = mmap(NULL, TPS_SIZE, PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
            new_mem_page->reference_counter = 1;
            mprotect(reference_tps->page_indirection->page_location, TPS_SIZE, PROT_READ);
            // the memory page should be a copy of the old one
            memcpy(new_mem_page->page_location, reference_tps->page_indirection->page_location, TPS_SIZE);
            // now actually copy the new material
            memcpy(new_mem_page->page_location + offset, buffer, length);
            mprotect(new_mem_page->page_location, TPS_SIZE, PROT_NONE);
            mprotect(reference_tps->page_indirection->page_location, TPS_SIZE, PROT_NONE);
            reference_tps->page_indirection = new_mem_page;

        }
        else {
            mprotect(reference_tps->page_indirection->page_location, TPS_SIZE, PROT_WRITE);
            memcpy(reference_tps->page_indirection->page_location + offset, buffer, length);
            mprotect(reference_tps->page_indirection->page_location, TPS_SIZE, PROT_NONE);
        }
        queue_enqueue(thread_to_tps, reference_tps);
        exit_critical_section();
        return 0;
    }
    else {
        exit_critical_section();
    }
    return -1;
}

int tps_clone(pthread_t tid)
{
    enter_critical_section();
    tps* reference_tps = NULL;
    pthread_t curr_thread = pthread_self();
    queue_iterate(thread_to_tps, find_matching_thread, (void*)&curr_thread, (void**)&reference_tps);
    if(reference_tps != NULL) { // There is already a tps associated with the current thread, return error
        exit_critical_section();
        return -1;
    }
    tps* reference_tps2 = NULL;
    pthread_t target_thread = tid;
    // find the tid given in the queue of tps, tps struct of this thread is stored in reference_tps2
    queue_iterate(thread_to_tps, find_matching_thread, (void*)&target_thread, (void**)&reference_tps2);
    if(reference_tps2 == NULL) { // Could not find a tps given the tid
        exit_critical_section();
        return -1;
    }
    tps* new_tps = calloc(1, sizeof(tps));
    new_tps->thread_id = pthread_self();
    // have the 2 tps share the page indirection until a write operation is performed
    new_tps->page_indirection = reference_tps2->page_indirection;
    new_tps->page_indirection->reference_counter += 1;
    queue_enqueue(thread_to_tps, new_tps);
    exit_critical_section();
    return 0;
}
