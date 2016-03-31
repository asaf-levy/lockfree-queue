#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <string.h>
#include "lf_queue.h"
#include "lf_shm_queue.h"

#define N_ELEM 10
#define N_ITER 1000000
#define N_THREADS 8
#define SHM_NAME "/shm_name"

static void enq_dec(lf_queue *q)
{
	int *val = lf_queue_dequeue(q);
	assert(val == NULL);

	for (int i = 0; i < N_ELEM; ++i) {
		val = lf_queue_get(q);
		assert(val != NULL);
		*val = i;
		lf_queue_enqueue(q, val);
	}
	val = lf_queue_get(q);
	assert(val == NULL);

	for (int i = 0; i < N_ELEM; ++i) {
		val = lf_queue_dequeue(q);
		assert(val != NULL);
		assert(*val == i);
		lf_queue_put(q, val);
	}

	val = lf_queue_dequeue(q);
	assert(val == NULL);
}

static void serial_test(void)
{
	lf_queue *q = lf_queue_init(N_ELEM, sizeof(int));
	assert(q != NULL);

	for (int i = 0; i < 10; ++i) {
		enq_dec(q);
	}

	lf_queue_destroy(q);
}

uint64_t g_enq_sum = 0;
uint64_t g_deq_sum = 0;

static void *enq_dec_task(void *arg)
{
	lf_queue *q = arg;
	int *val;

	for (int i = 0; i < N_ITER; ++i) {
//		if (i % 10000 == 0) {
//			fprintf(stderr, "Iteration %d\n", i);
//		}
		val = lf_queue_get(q);
		if (val != NULL) {
			*val = i;
			lf_queue_enqueue(q, val);
			__sync_add_and_fetch(&g_enq_sum, i);
		}

		val = lf_queue_dequeue(q);
		if (val != NULL) {
			__sync_add_and_fetch(&g_deq_sum, *val);
			lf_queue_put(q, val);
		}
	}
	return 0;
}

static void dec(lf_queue *q, bool block)
{
	int *val;

	for (int i = 0; i < N_ITER; ++i) {
//		if (i % 100000 == 0) {
//			fprintf(stderr, "Iteration %d\n", i);
//		}
		do {
			val = lf_queue_dequeue(q);
			if (val != NULL) {
				break;
			}
		} while(block);
		if (val != NULL) {
			__sync_add_and_fetch(&g_deq_sum, *val);
			lf_queue_put(q, val);
		}
	}
}

static void *dec_task(void *arg)
{
	lf_queue *q = arg;
	dec(q, false);
	return 0;
}

static void enq(lf_queue *q, bool block)
{
	int *val;

	for (int i = 0; i < N_ITER; ++i) {
//		if (i % 100000 == 0) {
//			fprintf(stderr, "Iteration %d\n", i);
//		}
		do {
			val = lf_queue_get(q);
			if (val != NULL) {
				break;
			}
		} while(block);
		if (val != NULL) {
			*val = i;
			lf_queue_enqueue(q, val);
			__sync_add_and_fetch(&g_enq_sum, i);
		}
	}
}

static void *enq_task(void *arg)
{
	lf_queue *q = arg;
	enq(q, false);
	return 0;
}


static void mt_test(void)
{
	int err;
	pthread_t threads[N_THREADS];
	lf_queue *q;
	struct timespec start;
	struct timespec end;

	q = lf_queue_init(N_ELEM, sizeof(int));
	assert(q != NULL);

	clock_gettime(CLOCK_REALTIME, &start);

	for (int i = 0; i < N_THREADS; ++i) {
		if (i % 3 == 0) {
			err = pthread_create(&threads[i], NULL, enq_dec_task, q);
		}
		if (i % 3 == 1) {
			err = pthread_create(&threads[i], NULL, enq_task, q);
		}
		if (i % 3 == 2) {
			err = pthread_create(&threads[i], NULL, dec_task, q);
		}
		assert(err == 0);
	}

	for (int i = 0; i < N_THREADS; ++i) {
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

static void shm_test(void)
{
	int pid;
	lf_shm_queue *shm_queue;
	lf_queue *queue;

	pid = fork();
	if (pid == 0) { // child
		sleep(1);
		shm_queue = lf_shm_queue_attach(SHM_NAME, N_ELEM, sizeof(int));
		assert(shm_queue != NULL);
		queue = lf_shm_queue_get_underlying_handle(shm_queue);
		enq(queue, true);
		lf_shm_queue_deattach(shm_queue);
	} else { // parent
		shm_queue = lf_shm_queue_init(SHM_NAME, N_ELEM, sizeof(int));
		assert(shm_queue != NULL);
		queue = lf_shm_queue_get_underlying_handle(shm_queue);
		g_deq_sum = 0;
		dec(queue, true);
		// the expected sum is the sum of the arithmetic progression
		// from 1 to N_ITER
		assert(g_deq_sum == ((uint64_t)N_ITER * (N_ITER - 1) / 2));
		waitpid(pid, NULL, 0);
		lf_shm_queue_destroy(shm_queue);
	}
}

#define MSG_SIZE 512
void basic_test()
{
	char *enq_msg;
	char *deq_msg;

	lf_queue *queue = lf_queue_init(5, MSG_SIZE);
	assert(queue != NULL);

	enq_msg = lf_queue_get(queue);
	assert(enq_msg != NULL);
	snprintf(enq_msg, MSG_SIZE, "hello world");
	lf_queue_enqueue(queue, enq_msg);

	deq_msg = lf_queue_dequeue(queue);
	assert(deq_msg != NULL);
	assert(strcmp(enq_msg, deq_msg) == 0);
	lf_queue_put(queue, deq_msg);

	lf_queue_destroy(queue);
}

int main(void)
{
	basic_test();
	serial_test();
	mt_test();
	shm_test();
	return 0;
}
