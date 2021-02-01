#  Submission Report Project 3:
## Semaphore Implementation:
For our semaphore implemnatation, we first started by defining a struct with 
members consisting of an integer count, a queue called waiting_threads for the 
line of threads waiting to access the semaphore's resources, and a single p_
threadt pointer called recent_block. 
*Sem create* starts with entering a critical section as it is accessing 
something that all the threads will share then callocs the necessary memory, 
sets recent_unblock to null, initializes the count with the value sent to the 
function and creates the queue of threads for waiting_threads. Sem destroy 
simply checks the necessary conditions of sem not being null and the waiting_
threads is empty before freeing the memory. 
*Sem up* enters the critical section when valid and increments the count. It 
dequeues a waiting thread if the queue is not empty since we just incremented 
the count. It then uses the returned thread to call unblock and sets recent_
unblock to the thread to be used in the corner case. Then it exits the 
critical section. 
*Sem down* enters a critical section when valid and checks if sem's count is 
0. If it is, it put the thread into waiting by calling pthread_self and 
enqueueing the thread followed by blocking itself. We used this code to catch 
the corner case:
```C
if (sem->count == 0 && sem->recent_unblock != NULL) {
	pthread_t self = pthread_self();
	if (*(sem->recent_unblock) == self) {
		queue_enqueue(sem->waiting_threads, &self);
		sem->recent_unblock = NULL;
		thread_block();
	}
}
```
it checks that count, upon return from unblocking, is 0. If it is, it'll know 
that its resouce was snatched before it could return from unblocking. Then it 
checks if a thread was recently unblocked with recent_unblock. Additionally, 
it confirms its own identity by checking pthread_self and recent_unblock to 
know that this thread is the one to reblock. If all met, it reblocks the 
thread and chances recent_unblock to NULL. Otherwise, it is free to decrement 
the count and exit the critical section. 
*Sem get val* gets checks the return value conditions before seeing that count 
greater than 0 results in sval to be changed to the count. Else, it sets sval 
to the negative length of the queue.
## Thread Protected Stack
For the TPS implementation, we will start by pointing out that for every TPS 
related function, we start the function by entering and finish it by exiting a 
critical section. 
Our implementation used two structs. One was a mem_page struct consisting of a 
page_location pointer to be used to access, through its address, the page data 
of the TPS and an integer called reference_counter to indicate how many TPS's 
are sharing the mem_page address. The other struct is the actual TPS struct 
consisting of a pthread_p variable called thread_id to identify the TPS's 
corresponding thread and a mem_page pointer called page_indirection that 
allows for the second layer of indirection to be used in CoW scenarios
*TPS init* checks if this the first TPS to be created, if it is, it creates a 
queue for the global variable __thread to tps__ which is used to keep track of 
all the public TPS's for the threads that have them, determines if protection 
error detection is necessary, and if it is, then it sets the proper signals.
*TPS create* starts by callocing a mempage and tps struct. To initialize the 
mem_page struct, mmap is called with a NULL address, the PROT_NONE flag, and 
the MAP_PRIVATE flags, and the resulting pointer is given to page_location 
while reference_counter is set to 1. To initialize thread_id in the TPS 
struct, *TPS create* calls self and sets the id to the returned value in order 
to link the TPS to the current thread. Finally linking the mem_page struct to 
the TPS is done through setting page_indirection to the mem_page struct's 
pointer created when callocing. Once the TPS and mem_page structs are created, 
the whole affair is enqueued into the __thread to tps__ global variable. 
For *TPS destroy*, our function first iterates through the queue __thread to 
tps__ and searches for a thread with the same id as the currently running 
thread using this segment of code:
```C
tps* reference_tps = NULL;
pthread_t curr_thread = pthread_self();
queue_iterate(thread_to_tps, find_matching_thread, (void*)&curr_thread, (void**
	)&reference_tps);
if(reference_tps != NULL)
```
After entering the critical section, the thread determines its own pthread id 
and searches through the queue with the current thread's id (curr_thread) 
using the static helper function *find matching threads* defined below that 
simply checks the TPS's id to the thread id of interest:
```C
static int find_matching_thread (void *data, void *arg){
    tps* temp_tps = (tps*)data;
    pthread_t thread_id = *(pthread_t *)arg;
    if (temp_tps->thread_id == thread_id)
        return 1;
    return 0;
}
```
If the thread is in the queue, which is checked using the returned pointer 
reference_tps (in the if statement two segments of code above), then it has a 
TPS and the function proceeds to delete the TPS from the queue, free the 
memory linked to the pointers and sets them to NULL.
*TPS read* starts by first checking that the there is a TPS associated with 
the current thread using same process as the *TPS destroy* function. If there 
is a TPS associated with the thread, then the following segment of code will 
run:
```C
mprotect(reference_tps->page_indirection->page_location, TPS_SIZE, PROT_READ);
memcpy(buffer, reference_tps->page_indirection->page_location + offset,length);
mprotect(reference_tps->page_indirection->page_location, TPS_SIZE, PROT_NONE);
```
It changes the protection for the pages to read using the page_location of the 
TPS's mem_page struct and the PROT_READ flag. The data is then copied into a 
the buffer address sent to the function offsetting by offset copying length 
bytes using memcpy and the proper variables. After the copy is complete, the 
protection is rechanged back to its original state of no protection using the 
PROT_NONE flag. If no thread was found in the queue, then the function simply 
returns -1 and does nothing else. 
*TPS write* begins with checking of the current thread has a TPS associated 
with it using the same method described in destroy. If there is a TPS, then 
the thread will be deleted from the queue of TPS's to allow for a possible 
changes in the contents of reference_tps. This is the implementation for 
writing:
```C
if(reference_tps->page_indirection->reference_counter > 1) {
    reference_tps->page_indirection->reference_counter--;
    mem_page* new_mem_page = calloc(1, sizeof(mem_page));
    new_mem_page->page_location = mmap(NULL, TPS_SIZE, PROT_WRITE, MAP_PRIVATE|
    	MAP_ANON, -1, 0);
    new_mem_page->reference_counter = 1;
    mprotect(reference_tps->page_indirection->page_location, TPS_SIZE,
    	PROT_READ);
    memcpy(new_mem_page->page_location, reference_tps->page_indirection->
    	page_location, TPS_SIZE);
    memcpy(new_mem_page->page_location + offset, buffer, length);
    mprotect(new_mem_page->page_location, TPS_SIZE, PROT_NONE);
    mprotect(reference_tps->page_indirection->page_location, TPS_SIZE, 
    	PROT_NONE);
    reference_tps->page_indirection = new_mem_page;
}
else {
    mprotect(reference_tps->page_indirection->page_location, TPS_SIZE, 
    	PROT_WRITE);
    memcpy(reference_tps->page_indirection->page_location + offset, buffer, 
    	length);
    mprotect(reference_tps->page_indirection->page_location, TPS_SIZE, 
    	PROT_NONE);
}
```
First the counter of the current shared mem_page is checked to be greater than 
one. Once it confirms, the function decrements the counter and callocs the 
necessary memory for a mem_page then calling mmap with PROT_WRITE flag to 
allow for this new page to be written to. The reference counter of the page is 
set to 1, and the protection of the currenty thread whose mem_page is to be 
changed to a private copy but still refers to the shared one is changed to 
read to allow the copying. The contents are copied from the shared page to the 
newest one using memcpy, and then the writing occurs using the new mem_page
offset by the value offset using the contents of buffer. Finally, the 
protections for the shared and new mem_page are reverted and the new mem_page 
is assigned to the reference_tps. If the counter isn't greater than 1, the 
function simply changes protection, calls memcpy using the buffer, and changes 
the protection back.
*TPS clone* first checks errors by ensuring that the current thread doesn't 
have a TPS associated with it and the tid sent in actually has a TPS 
associated with it calling it reference_tps2. Both use the same searching 
function described in destroy. 
```C
    tps* new_tps = calloc(1, sizeof(tps));
    new_tps->thread_id = pthread_self();
    new_tps->page_indirection = reference_tps2->page_indirection;
    new_tps->page_indirection->reference_counter += 1;
    queue_enqueue(thread_to_tps, new_tps);
    exit_critical_section();
```
Then a new tps is calloced with the thread id being set to the current 
thread's id to associate the TPS with the thread. Then in order to share the 
page structure with that of the target thread (defined by tid), the new tps's 
indirection pointer is set to the reference_tps2, page_indirection pointer and 
the counter is incremented before the new tps is enqueued.
## TPS Testing
To start out testing, we simply created and destroyed a TPS and checked return 
values are 0. The *test invalid offset length* function, uses offset and 
length values of size TPS_SIZE+1 in the *tps read* and *tps write* to get 
return values of -1. *test pre creation* attempts to use the read and write 
functions before a tps has been created and checks the return values.*test int*
function write 10 integers into the TPS to check that the API is able to 
work with other datatypes. The function then reads in one value at a time 
using a for loop and compares the read values to the original array used to 
write the values. If all ten values match, then the function returns a success 
statement, otherwise a failure statement. *test multiple rw* writes a long 
string to the TPS and prints it. It then capitalizes two words in the TPS 
using hard coded write functions and prints the new statement. For extra 
testing, the function also compares the areas that were written to to the 
strings that were supposed to be written and asserts that they are equal. 
*complex1* uses two threads one (thread2a) has a TPS and writes to it, the 
other (thread 1a) clones the TPS and prints the contents which should match. 
Then thread1a writes again to its TPS and returns to thread2a to show that its 
TPS is unaffected by this write. The final test in *tps tester* is a simple 
protection check where a thread attempts to illegally access its protected 
stack, which causes a protected error fault. 
The second tester we provided, is just a few more random clones, reads, and 
writes. Thread1 creates thread2 which creates thread3. Thread3 writes a line 
to memory, modifies it, then thread2 clones the TPS and modifies the string. 
All these operations should work properly, and then a protected memory access 
should be made from another thread and the program should exit displaying the 
protected stack error
