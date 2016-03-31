# lockfree-queue
A preallocted lock free queue implementation written in C

The queue allows its user to get a pointer to the preallocated data before enqeuing it in order to avoid memory copies.

**Usage example**

	#define MSG_SIZE 512
	lf_queue *queue = lf_queue_init(5, MSG_SIZE);
	assert(queue != NULL);

	char *enq_msg = lf_queue_get(queue);
	assert(enq_msg != NULL);
	snprintf(enq_msg, MSG_SIZE, "hello world");
	lf_queue_enqueue(queue, enq_msg);

	char *deq_msg = lf_queue_dequeue(queue);
	assert(deq_msg != NULL);
	assert(strcmp(enq_msg, deq_msg) == 0);
	lf_queue_put(queue, deq_msg);

	lf_queue_destroy(queue);

##Build Instructions
1. Install CMake
2. mkdir build
3. cd build
4. cmake ../
5. make

Run tests with "make test"

To install run "make install"
