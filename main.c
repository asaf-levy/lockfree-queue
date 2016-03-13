#include "lf_queue.h"
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <pthread.h>

#define N_ELEM 1000
#define N_ITER 1000000
#define N_THREADS 9

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

	struct timespec start;
	struct timespec end;
	clock_gettime(CLOCK_REALTIME, &start);
	for (i = 0; i < N_ITER; ++i) {
		if (i % 100000 == 0) {
			fprintf(stderr, "Iteration %d\n", i);
		}
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
	clock_gettime(CLOCK_REALTIME, &end);
	printf("thread done sec=%lu msec=%lu\n", end.tv_sec - start.tv_sec,
	       (end.tv_nsec - start.tv_nsec) / 1000000);
	return 0;
}

void *dec_task(void *arg)
{
	lf_queue_handle_t q = (lf_queue_handle_t)arg;
	int i;
	int err;
	int *val;
	lf_element_t *e;

	for (i = 0; i < N_ITER; ++i) {
		if (i % 100000 == 0) {
			fprintf(stderr, "Iteration %d\n", i);
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

void *enq_task(void *arg)
{
	lf_queue_handle_t q = (lf_queue_handle_t)arg;
	int i;
	int err;
	int *val;
	lf_element_t *e;

	for (i = 0; i < N_ITER; ++i) {
		if (i % 100000 == 0) {
			fprintf(stderr, "Iteration %d\n", i);
		}
		err = lf_queue_get(q, &e);
		if (err == 0) {
			val = e->data;
			*val = i;
			lf_queue_enqueue(q, e);
			__sync_add_and_fetch(&g_enq_sum, i);
		}
	}
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
	dec_task((void *) q);
	clock_gettime(CLOCK_REALTIME, &end);

	printf("all threads finished g_enq_sum=%lu g_deq_sum=%lu\n", g_enq_sum, g_deq_sum);
	printf("n_threads=%d n_iter=%d test time msec=%lu\n", N_THREADS, N_ITER,
	       (((end.tv_sec - start.tv_sec) * 1000000000) + end.tv_nsec - start.tv_nsec) / 1000000);
	assert(g_enq_sum == g_deq_sum);

	lf_queue_destroy(q);

}

int main(void)
{
	serial_test();
	mt_test();
	return 0;
}
