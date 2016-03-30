#include "lf_shm_queue.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

struct lf_shm_queue {
    lf_queue *queue;
    size_t mem_size;
    char shm_name[256];
    void *shm_ptr;
};

lf_shm_queue *lf_shm_queue_init(const char *shm_name, size_t n_elements,
                                size_t element_size)
{
	int shm_fd;
	int res;
	lf_shm_queue *shm_queue;

	shm_queue = malloc(sizeof(lf_shm_queue));
	if (shm_queue == NULL) {
		return NULL;
	}

	shm_queue->mem_size = lf_queue_get_required_memory(n_elements,
	                                                   element_size);

	strncpy(shm_queue->shm_name, shm_name, sizeof(shm_queue->shm_name));
	shm_fd = shm_open(shm_queue->shm_name, O_CREAT | O_RDWR,
	                  S_IRUSR | S_IWUSR);
	if (shm_fd < 0) {
		free(shm_queue);
		printf("shm_open failed err=%d\n", errno);
		return NULL;
	}
	shm_queue->shm_ptr = NULL;

	res = ftruncate(shm_fd, shm_queue->mem_size);
	if (res != 0) {
		close(shm_fd);
		free(shm_queue);
		printf("ftruncate failed err=%d\n", errno);
		return NULL;
	}
	shm_queue->shm_ptr = mmap(NULL, shm_queue->mem_size,
	                          PROT_READ | PROT_WRITE,
	                          MAP_SHARED, shm_fd, 0);
	close(shm_fd);
	if (shm_queue->shm_ptr == MAP_FAILED) {
		free(shm_queue);
		printf("mmap failed err=%d", errno);
		return NULL;
	}

	shm_queue->queue = lf_queue_mem_init(shm_queue->shm_ptr, n_elements,
	                                     element_size);
	if (shm_queue->queue == NULL) {
		free(shm_queue);
		printf("lf_queue_mem_init failed\n");
		return NULL;
	}
	return shm_queue;
}

lf_shm_queue *lf_shm_queue_attach(const char *shm_name, size_t n_elements,
                                  size_t element_size)
{
	int shm_fd;

	lf_shm_queue *shm_queue = malloc(sizeof(lf_shm_queue));
	if (shm_queue == NULL) {
		return NULL;
	}

	shm_queue->mem_size = lf_queue_get_required_memory(n_elements,
	                                                   element_size);
	shm_queue->shm_ptr = NULL;
	strncpy(shm_queue->shm_name, shm_name, sizeof(shm_queue->shm_name));
	shm_fd = shm_open(shm_queue->shm_name, O_RDWR, S_IRUSR | S_IWUSR);
	if (shm_fd < 0) {
		free(shm_queue);
		printf("shm_open failed err=%d\n", errno);
		return NULL;
	}

	shm_queue->shm_ptr = mmap(NULL, shm_queue->mem_size,
	                          PROT_READ | PROT_WRITE,
	                          MAP_SHARED, shm_fd, 0);
	close(shm_fd);
	if (shm_queue->shm_ptr == MAP_FAILED) {
		free(shm_queue);
		printf("mmap failed err=%d\n", errno);
		return NULL;
	}
	shm_queue->queue = lf_queue_attach(shm_queue->shm_ptr);
	if (shm_queue->queue == NULL) {
		free(shm_queue);
		return NULL;
	}

	return shm_queue;
}

static void terminate(lf_shm_queue *queue)
{
	int res;

	lf_queue_destroy(queue->queue);
	res = munmap(queue->shm_ptr, queue->mem_size);
	if (res != 0) {
		printf("munmap failed err=%d\n", errno);
	}
}

int lf_shm_queue_deattach(lf_shm_queue *queue)
{
	terminate(queue);
	free(queue);
	return 0;
}

int lf_shm_queue_destroy(lf_shm_queue *queue)
{
	int res;

	terminate(queue);
	res = shm_unlink(queue->shm_name);
	if (res != 0) {
		printf("shm_unlink failed err=%d\n", errno);
	}
	free(queue);
	return 0;
}

lf_queue *lf_shm_queue_get_underlying_handle(lf_shm_queue *queue)
{
	return queue->queue;
}
