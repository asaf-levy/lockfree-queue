#include "lf_queue.h"
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>

#define N_ELEM 500


void enq_dec(lf_queue_t *q)
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
		printf("val=%d\n", *val);
		lf_queue_put(q, e);
	}
	err = lf_queue_dequeue(q, &e);
	assert(err == ENOMEM);
}

void serial_test(void)
{
	int i;
	lf_queue_t q;
	int err = lf_queue_init(&q, N_ELEM, sizeof(int));
	assert(err == 0);

	for (i = 0; i < 10; ++i) {
		enq_dec(&q);
	}

	lf_queue_destroy(&q);
}

uint64_t g_enq_sum = 0;
uint64_t g_deq_sum = 0;

#define MT_N_ELEM 100000
#define MT_N_THREADS 2

void *enq_dec_task(void *arg)
{
	lf_queue_t *q = arg;
	int i;
	int err;
	int *val;
	lf_element_t *e;

	for (i = 0; i < MT_N_ELEM; ++i) {
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
	printf("thread done\n");
	return 0;
}

void mt_test(void)
{
	int i;
	int err;
	pthread_t threads[MT_N_THREADS];
	lf_queue_t q;

	err = lf_queue_init(&q, N_ELEM, sizeof(int));
	assert(err == 0);

	for (i = 0; i < MT_N_THREADS; ++i) {
		err = pthread_create(&threads[i], NULL, enq_dec_task, &q);
		assert(err == 0);
	}

	for (i = 0; i < MT_N_THREADS; ++i) {
		pthread_join(threads[i], NULL);
	}
	printf("all threads finished g_enq_sum=%lu g_deq_sum=%lu\n", g_enq_sum, g_deq_sum);
	assert(g_enq_sum == g_deq_sum);

	lf_queue_destroy(&q);

}

int main(void)
{
//	serial_test();
	mt_test();
	return 0;
}
