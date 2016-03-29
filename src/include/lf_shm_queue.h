#pragma once

#include <stddef.h>
#include <stdint.h>
#include "lf_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lf_shm_queue lf_shm_queue;

// init a lock free queue on top of a shared memory segment
lf_shm_queue *lf_shm_queue_init(const char *shm_name, size_t n_elements, size_t element_size);
// attach to an already initialized queue on top of a shared memory segment
lf_shm_queue *lf_shm_queue_attach(const char *shm_name, size_t n_elements, size_t element_size);
// de attach from a previously attached queue
int lf_shm_queue_deattach(lf_shm_queue *queue);
// destroy the queue,
int lf_shm_queue_destroy(lf_shm_queue *queue);

// obtain a handle to the underlying lock free queue
lf_queue *lf_shm_queue_get_underlying_handle(lf_shm_queue *queue);

#ifdef __cplusplus
}
#endif
