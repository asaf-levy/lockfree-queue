#ifndef __IF_QUEUE_H_INCLUDED__
#define __IF_QUEUE_H_INCLUDED__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t lf_queue_handle_t;

typedef struct lf_element {
    void *data;
} lf_element_t;

// return the amount of memory required for a lock free queue
size_t lf_queue_get_required_memory(size_t n_elements, size_t element_size);

// init the lock free queue from a pre allocated chunk of memory
// can be used in order to initialized the queue on a shared memory segment
int lf_queue_mem_init(lf_queue_handle_t *queue, void *mem, size_t n_elements, size_t element_size);
// allocates memory and init the lock free queue
int lf_queue_init(lf_queue_handle_t *queue, size_t n_elements, size_t element_size);
// attach to an already initialized queue on the specified memory ptr
int lf_queue_attach(lf_queue_handle_t *queue, void *mem);
// destroy the queue and deallocate its memory
void lf_queue_destroy(lf_queue_handle_t queue);

// get an element from the queue that to be later on queued by calling lf_queue_enqueue
// will fail with ENOMEM in case all of the elements in the queue are already in use
int lf_queue_get(lf_queue_handle_t queue, lf_element_t **element);
// enqueue an element that was obtained by a call to lf_queue_get
void lf_queue_enqueue(lf_queue_handle_t queue, lf_element_t *element);
// dequeue an element from the queue, the element must be later returned by a call
// to lf_queue_put
int lf_queue_dequeue(lf_queue_handle_t queue, lf_element_t **element);
// return an element to the queue that was obtained by a call lf_queue_dequeue
void lf_queue_put(lf_queue_handle_t queue, lf_element_t *element);

#ifdef __cplusplus
}
#endif

#endif
