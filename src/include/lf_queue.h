#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lf_queue lf_queue;

// return the amount of memory required for a lock free queue
size_t lf_queue_get_required_memory(size_t n_elements, size_t element_size);
// init the lock free queue from a pre allocated chunk of memory
// can be used in order to initialized the queue on a shared memory segment
// The maximal value of n_elements is 2^32
lf_queue *lf_queue_mem_init(void *mem, size_t n_elements, size_t element_size);
// allocates memory and init the lock free queue
lf_queue *lf_queue_init(size_t n_elements, size_t element_size);
// attach to an already initialized queue on the specified memory ptr
lf_queue *lf_queue_attach(void *mem);
// destroy the queue and deallocate its memory
void lf_queue_destroy(lf_queue *queue);
// get an element from the queue that to be later on queued by calling lf_queue_enqueue
// will fail with ENOMEM in case all of the elements in the queue are already in use
void *lf_queue_get(lf_queue *queue);
// enqueue an element that was obtained by a call to lf_queue_get
void lf_queue_enqueue(lf_queue *queue, void *data);
// dequeue an element from the queue, the element must be later returned by a call
// to lf_queue_put
void *lf_queue_dequeue(lf_queue *queue);
// return an element to the queue that was obtained by a call lf_queue_dequeue
void lf_queue_put(lf_queue *queue, void *data);

#ifdef __cplusplus
}
#endif
