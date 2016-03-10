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

int lf_queue_init(lf_queue_handle_t *queue, size_t n_elements, size_t element_size);
void lf_queue_destroy(lf_queue_handle_t queue);

// get an element from the queue that to be later on queued by calling lf_queue_enqueue
// will fail with ENOMEM in case all of the elements in the queue are already in use
int lf_queue_get(lf_queue_handle_t queue, lf_element_t **element);
// enqueue an element
void lf_queue_enqueue(lf_queue_handle_t queue, lf_element_t *element);
// dequeue an element
int lf_queue_dequeue(lf_queue_handle_t queue, lf_element_t **element);
// return an element to the queue following a call lf_queue_dequeue
void lf_queue_put(lf_queue_handle_t queue, lf_element_t *element);

#ifdef __cplusplus
}
#endif

#endif
