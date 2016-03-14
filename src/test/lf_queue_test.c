#include "lf_queue.h"
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <stdbool.h>

#define N_ELEM 1000
#define N_ITER 1000000
#define N_THREADS 9
#define SHM_NAME "/shm_name"

void enq_dec(lf_queue_handle_t q)
{
	int i;
	int err;
	int *val;
	lf_element_t *e;

	err = lf_queue_dequeue(q, &e);
	assert(err == ENOMEM);

	for (i = 0; i < N_ELEM; ++i) {
		err = lf_queue_get(q, &e);
		assert(err == 0);
		val = e->data;
		*val = i;
		lf_queue_enqueue(q, e);
	}
	err = lf_queue_get(q, &e);
	assert(err == ENOMEM);

	for (i = 0; i < N_ELEM; ++i) {
		err =  lf_queue_dequeue(q, &e);
		assert(err == 0);
		val = e->data;
		assert(*val == i);
		lf_queue_put(q, e);
	}

	err = lf_queue_dequeue(q, &e);
	assert(err == ENOMEM);
}

void serial_test(void)
{
	int i;
	lf_queue_handle_t q;
	int err = lf_queue_init(&q, N_ELEM, sizeof(int));
	assert(err == 0);

	for (i = 0; i < 10; ++i) {
		enq_dec(q);
	}

	lf_queue_destroy(q);
}

uint64_t g_enq_sum = 0;
uint64_t g_deq_sum = 0;

void *enq_dec_task(void *arg)
{
	lf_queue_handle_t q = (lf_queue_handle_t)arg;
	int i;
	int err;
	int *val;
	lf_element_t *e;

	for (i = 0; i < N_ITER; ++i) {
//		if (i % 10000 == 0) {
//			fprintf(stderr, "Iteration %d\n", i);
//		}
		err = lf_queue_get(q, &e);
		if (err == 0) {
			val = e->data;
			*val = i;
			lf_queue_enqueue(q, e);
			__sync_add_and_fetch(&g_enq_sum, i);
		}

		err =  lf_queue_dequeue(q, &e);
		if (err == 0) {
			val = e->data;
			__sync_add_and_fetch(&g_deq_sum, *val);
			lf_queue_put(q, e);
		}
	}
	return 0;
}

void dec(lf_queue_handle_t q, bool block)
{
	int i;
	int *val;
	lf_element_t *e;
	int res;

	for (i = 0; i < N_ITER; ++i) {
//		if (i % 100000 == 0) {
//			fprintf(stderr, "Iteration %d\n", i);
//		}
		do {
			res = lf_queue_dequeue(q, &e);
			if (res == 0) {
				break;
			}
		} while(block);
		if (res == 0) {
			val = e->data;
			__sync_add_and_fetch(&g_deq_sum, *val);
			lf_queue_put(q, e);
		}
	}
}

void *dec_task(void *arg)
{
	lf_queue_handle_t q = (lf_queue_handle_t)arg;
	dec(q, false);
	return 0;
}

void enq(lf_queue_handle_t q, bool block)
{
	int i;
	int *val;
	lf_element_t *e;
	int res = 0;

	for (i = 0; i < N_ITER; ++i) {
//		if (i % 100000 == 0) {
//			fprintf(stderr, "Iteration %d\n", i);
//		}
		do {
			res = lf_queue_get(q, &e);
			if (res == 0) {
				break;
			}
		} while(block);
		if (res == 0) {
			val = e->data;
			*val = i;
			lf_queue_enqueue(q, e);
			__sync_add_and_fetch(&g_enq_sum, i);
		}
	}
}

void *enq_task(void *arg)
{
	lf_queue_handle_t q = (lf_queue_handle_t)arg;
	enq(q, false);
	return 0;
}


void mt_test(void)
{
	int i;
	int err;
	pthread_t threads[N_THREADS];
	lf_queue_handle_t q;
	struct timespec start;
	struct timespec end;

	err = lf_queue_init(&q, N_ELEM, sizeof(int));
	assert(err == 0);

	clock_gettime(CLOCK_REALTIME, &start);

	for (i = 0; i < N_THREADS; ++i) {
		if (i % 3 == 0) {
			err = pthread_create(&threads[i], NULL, enq_dec_task, (void *)q);
		}
		if (i % 3 == 1) {
			err = pthread_create(&threads[i], NULL, enq_task, (void *)q);
		}
		if (i % 3 == 2) {
			err = pthread_create(&threads[i], NULL, dec_task, (void *)q);
		}
		assert(err == 0);
	}

	for (i = 0; i < N_THREADS; ++i) {
		pthread_join(threads[i], NULL);
	}
	dec(q, false);
	clock_gettime(CLOCK_REALTIME, &end);

	printf("all threads finished g_enq_sum=%lu g_deq_sum=%lu\n", g_enq_sum, g_deq_sum);
	printf("n_threads=%d n_iter=%d test time msec=%lu\n", N_THREADS, N_ITER,
	       (((end.tv_sec - start.tv_sec) * 1000000000) + end.tv_nsec - start.tv_nsec) / 1000000);
	assert(g_enq_sum == g_deq_sum);

	lf_queue_destroy(q);

}

void shm_test(void)
{
	int err;
	void *rptr;
	size_t mem_size;
	int pid;
	int fd;
	lf_queue_handle_t queue;

	mem_size = lf_queue_get_required_memory(N_ELEM, sizeof(int));
	pid = fork();
	if (pid == 0) { // child
		sleep(1);
		fd = shm_open(SHM_NAME, O_RDWR, S_IRUSR | S_IWUSR);
		assert(fd >= 0);
		rptr = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		assert(rptr != MAP_FAILED);
		err = lf_queue_attach(&queue, rptr);
		assert(err == 0);
		enq(queue, true);
		err = munmap(rptr, mem_size);
		assert(err == 0);
	} else { // parent
		fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
		assert(fd >= 0);

		err = ftruncate(fd, mem_size);
		assert(err == 0);
		rptr = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		assert(rptr != MAP_FAILED);
		err = lf_queue_mem_init(&queue, rptr, N_ELEM, sizeof(int));
		assert(err == 0);
		g_deq_sum = 0;
		dec(queue, true);
		// the expected sum is the sum of the arithmetic progression
		// from 1 to N_ITER
		assert(g_deq_sum == ((uint64_t)N_ITER * (N_ITER - 1) / 2));
		err = munmap(rptr, mem_size);
		assert(err == 0);
		waitpid(pid, NULL, 0);
		err = shm_unlink(SHM_NAME);
		assert(err == 0);
	}
}

int main(void)
{
	serial_test();
	mt_test();
	shm_test();
	return 0;
}
