#include "lf_queue.h"
#include <stdio.h>
#include <assert.h>
#include <errno.h>

#define N_ELEM 10

int main(void)
{
	int i;
	int *val;
	lf_queue_t q;
	lf_element_t *e;
	int err = lf_queue_init(&q, N_ELEM, sizeof(int));
	assert(err == 0);

	err = lf_queue_dequeue(&q, &e);
	assert(err == ENOMEM);

	for (i = 0; i < N_ELEM; ++i) {
		err = lf_queue_get(&q, &e);
		assert(err == 0);
		val = e->data;
		*val = i;
		lf_queue_enqueue(&q, e);
	}
	err = lf_queue_get(&q, &e);
	assert(err == ENOMEM);

	for (i = 0; i < N_ELEM; ++i) {
		err =  lf_queue_dequeue(&q, &e);
		assert(err == 0);
		val = e->data;
		assert(*val == i);
		printf("val=%d\n", *val);
		lf_queue_put(&q, e);
	}
	err = lf_queue_dequeue(&q, &e);
	assert(err == ENOMEM);

	lf_queue_destroy(&q);

	return 0;
}