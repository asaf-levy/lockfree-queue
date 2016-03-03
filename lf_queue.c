#include "lf_queue.h"

#include <malloc.h>
#include <errno.h>
#include <assert.h>

typedef struct lf_element_impl lf_element_impl_t;

typedef struct lf_element_impl {
    lf_element_impl_t *next;
    void *data;
} lf_element_impl_t;

typedef struct lf_queue_impl {
    lf_element_impl_t *elements;
    size_t n_elements;
    size_t element_size;
    lf_element_impl_t *free_head;
    lf_element_impl_t *queue_head;
    lf_element_impl_t *queue_tail;
} lf_queue_impl_t;

static inline size_t raw_elem_size(size_t element_size)
{
	return element_size + sizeof(lf_element_impl_t);
}

int lf_queue_init(lf_queue_t *queue, size_t n_elements, size_t element_size)
{
	// TODO malloc checks
	size_t i;
	lf_element_impl_t *curr;
	lf_queue_impl_t *qimpl = malloc(sizeof(lf_queue_impl_t));
	qimpl->n_elements = n_elements;
	qimpl->element_size = element_size;
	qimpl->elements = calloc(n_elements, raw_elem_size(element_size));
	qimpl->free_head = qimpl->elements;
	qimpl->queue_head = NULL;
	qimpl->queue_tail = NULL;

	assert(n_elements > 0);
	assert(element_size > 0);
	assert(queue != NULL);

	curr = qimpl->elements;
	for (i = 0; i < n_elements; ++i) {
		curr->next = (void*)curr + raw_elem_size(element_size);
		curr->data = &curr->next + sizeof(curr->next);
		curr = curr->next;
	}
	curr->next = NULL;
	queue->queue = qimpl;
	return 0;
}

int lf_queue_destroy(lf_queue_t *queue)
{
	lf_queue_impl_t *qimpl = queue->queue;

	assert(queue->queue != NULL);
	free(qimpl->elements);
	free(qimpl);
	queue->queue = NULL;
	return 0;
}

int lf_queue_get(lf_queue_t *queue, lf_element_t **element)
{
	lf_queue_impl_t *qimpl = queue->queue;
	lf_element_impl_t *curr_free = __sync_fetch_and_or(&qimpl->free_head, 0);
	lf_element_impl_t *prev_val;

	while (curr_free) {
		prev_val = __sync_val_compare_and_swap(&qimpl->free_head,
		                                       curr_free, curr_free->next);
		if (prev_val == curr_free) {
			// swap was successful
			*element = (lf_element_t *)curr_free;
			(*element)->data = curr_free->data;
			return 0;
		}
		curr_free = prev_val;
	}
	return ENOMEM;
}

int lf_queue_enqueue(lf_queue_t *queue, lf_element_t *element)
{
	return 0;
}

int lf_queue_dequeue(lf_queue_t *queue, lf_element_t *element)
{
	return 0;
}
