#include "lf_shm_queue.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct lf_shm_queue_impl {
    lf_queue_t lf_queue;
    size_t mem_size;
    char shm_name[256];
    void *shm_ptr;
} lf_shm_queue_impl_t;


int lf_shm_queue_init(lf_shm_queue_handle_t *queue, const char *shm_name,
                      size_t n_elements, size_t element_size)
{
	lf_shm_queue_impl_t *qimpl;
	int shm_fd;
	int res;

	qimpl = malloc(sizeof(lf_shm_queue_impl_t));
	if (qimpl == NULL) {
		return ENOMEM;
	}

	qimpl->mem_size = lf_queue_get_required_memory(n_elements, element_size);

	strncpy(qimpl->shm_name, shm_name, sizeof(qimpl->shm_name));
	shm_fd = shm_open(qimpl->shm_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (shm_fd < 0) {
		free(qimpl);
		printf("shm_open failed err=%d\n", errno);
		return errno;
	}
	qimpl->shm_ptr = NULL;

	res = ftruncate(shm_fd, qimpl->mem_size);
	if (res != 0) {
		close(shm_fd);
		free(qimpl);
		printf("ftruncate failed err=%d\n", errno);
		return errno;
	}
	qimpl->shm_ptr = mmap(NULL, qimpl->mem_size, PROT_READ | PROT_WRITE,
	                      MAP_SHARED, shm_fd, 0);
	close(shm_fd);
	if (qimpl->shm_ptr == MAP_FAILED) {
		free(qimpl);
		printf("mmap failed err=%d", errno);
		return errno;
	}

	res = lf_queue_mem_init(&qimpl->lf_queue, qimpl->shm_ptr, n_elements,
	                        element_size);
	if (res != 0) {
		free(qimpl);
		printf("lf_queue_mem_init failed err=%d\n", res);
		return res;
	}
	queue->handle = qimpl;
	return 0;
}

int lf_shm_queue_attach(lf_shm_queue_handle_t *queue, const char *shm_name,
                        size_t n_elements, size_t element_size)
{
	lf_shm_queue_impl_t *qimpl;
	int res;
	int shm_fd;

	qimpl = malloc(sizeof(lf_shm_queue_impl_t));
	if (qimpl == NULL) {
		return ENOMEM;
	}

	qimpl->mem_size = lf_queue_get_required_memory(n_elements, element_size);
	qimpl->shm_ptr = NULL;
	strncpy(qimpl->shm_name, shm_name, sizeof(qimpl->shm_name));
	shm_fd = shm_open(qimpl->shm_name, O_RDWR, S_IRUSR | S_IWUSR);
	if (shm_fd < 0) {
		free(qimpl);
		printf("shm_open failed err=%d\n", errno);
		return errno;
	}

	qimpl->shm_ptr = mmap(NULL, qimpl->mem_size, PROT_READ | PROT_WRITE,
	                      MAP_SHARED, shm_fd, 0);
	close(shm_fd);
	if (qimpl->shm_ptr == MAP_FAILED) {
		free(qimpl);
		printf("mmap failed err=%d\n", errno);
		return errno;
	}
	res = lf_queue_attach(&qimpl->lf_queue, qimpl->shm_ptr);
	if (res != 0) {
		free(qimpl);
		return res;
	}

	queue->handle = qimpl;
	return 0;
}

static void terminate(lf_shm_queue_impl_t *qimpl)
{
	int res;

	lf_queue_destroy(qimpl->lf_queue);
	res = munmap(qimpl->shm_ptr, qimpl->mem_size);
	if (res != 0) {
		printf("munmap failed err=%d\n", errno);
	}
}

int lf_shm_queue_deattach(lf_shm_queue_handle_t queue)
{
	lf_shm_queue_impl_t *qimpl = queue.handle;
	terminate(qimpl);
	free(qimpl);
	return 0;
}

int lf_shm_queue_destroy(lf_shm_queue_handle_t queue)
{
	int res;
	lf_shm_queue_impl_t *qimpl = queue.handle;

	terminate(qimpl);
	res = shm_unlink(qimpl->shm_name);
	if (res != 0) {
		printf("shm_unlink failed err=%d\n", errno);
	}
	free(qimpl);
	return 0;
}

lf_queue_t lf_shm_queue_get_underlying_handle(lf_shm_queue_handle_t queue)
{
	lf_shm_queue_impl_t *qimpl = queue.handle;
	return qimpl->lf_queue;
}
