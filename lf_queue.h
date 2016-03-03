#ifndef __IF_QUEUE_H_INCLUDED__
#define __IF_QUEUE_H_INCLUDED__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lf_queue {
    void *queue;
} lf_queue_t;

typedef struct lf_element {
    void *data;
} lf_element_t;

int lf_queue_init(lf_queue_t *queue, size_t n_elements, size_t element_size);
int lf_queue_destroy(lf_queue_t *queue);

// get an element from the queue that to be later on queued by calling lf_queue_enqueue
int lf_queue_get(lf_queue_t *queue, lf_element_t **element);
int lf_queue_enqueue(lf_queue_t *queue, lf_element_t *element);
int lf_queue_dequeue(lf_queue_t *queue, lf_element_t *element);

#ifdef __cplusplus
}
#endif

#endif
