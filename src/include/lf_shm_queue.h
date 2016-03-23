#ifndef __IF_SHM_QUEUE_H_INCLUDED__
#define __IF_SHM_QUEUE_H_INCLUDED__

#include <stddef.h>
#include <stdint.h>
#include "lf_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t lf_shm_queue_handle_t;

// init a lock free queue on top of a shared memory segment
int lf_shm_queue_init(lf_shm_queue_handle_t *queue, const char *shm_name,
                       size_t n_elements, size_t element_size);
// attach to an already initialized queue on top of a shared memory segment
int lf_shm_queue_attach(lf_shm_queue_handle_t *queue, const char *shm_name,
                        size_t n_elements, size_t element_size);

int lf_shm_queue_destroy(lf_shm_queue_handle_t queue);

lf_queue_handle_t lf_shm_queue_get_underlying_handle(lf_shm_queue_handle_t queue);

#ifdef __cplusplus
}
#endif

#endif