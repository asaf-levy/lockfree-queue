#include "lf_queue.h"

#include <malloc.h>
#include <errno.h>
#include <assert.h>
#include <stdbool.h>

typedef struct lf_element_impl lf_element_impl_t;

typedef struct lf_element_impl {
    lf_element_t elem;
    lf_element_impl_t *next;
    // followed by the element data
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

	printf("qimpl->elements=%p\n", qimpl->elements);
	curr = qimpl->elements;
	for (i = 0; i < n_elements - 1; ++i) {
		curr->next = (lf_element_impl_t *) ((char*)curr + raw_elem_size(element_size));
		curr->elem.data = (void*)&curr->next + sizeof(curr->next);
		printf("curr=%p curr->next=%p data=%p diff=%d\n", curr, curr->next, curr->elem.data, (int)((char*)curr->next - (char*)curr));
		curr = curr->next;
	}
	printf("qimpl->elements=%p\n", qimpl->elements);
	curr->next = NULL;
	curr->elem.data = &curr->elem + sizeof(curr->elem);
	queue->queue = qimpl;
	return 0;
}

void lf_queue_destroy(lf_queue_t *queue)
{
	lf_queue_impl_t *qimpl = queue->queue;

	assert(queue->queue != NULL);
	free(qimpl->elements);
	free(qimpl);
	queue->queue = NULL;
}

int lf_queue_get(lf_queue_t *queue, lf_element_t **element)
{
	lf_queue_impl_t *qimpl = queue->queue;
	lf_element_impl_t *curr_free = __sync_fetch_and_or(&qimpl->free_head, 0);
	lf_element_impl_t *prev_val;

	while (curr_free) {
		printf("curr_free=%p curr_free->next=%p\n", curr_free, curr_free->next);
		prev_val = __sync_val_compare_and_swap(&qimpl->free_head,
		                                       curr_free, curr_free->next);
		if (prev_val == curr_free) {
			// swap was successful
			curr_free->next = NULL;
			*element = &curr_free->elem;
			return 0;
		}
		curr_free = prev_val;
	}
	return ENOMEM;
}

#define container_of(ptr, container, member) \
	((container *)((char *)ptr - offsetof(container, member)))

void lf_queue_enqueue(lf_queue_t *queue, lf_element_t *element)
{
	lf_queue_impl_t *qimpl = queue->queue;
	lf_element_impl_t *e_impl = container_of(element, lf_element_impl_t, elem);
	lf_element_impl_t *curr_head = __sync_fetch_and_or(&qimpl->queue_head, 0);
	lf_element_impl_t *curr_tail = __sync_fetch_and_or(&qimpl->queue_tail, 0);
	lf_element_impl_t *prev_val;

	printf("enqueue %d\n", *(int*)element->data);

	while (curr_head == NULL && curr_tail == NULL) {
		prev_val = __sync_val_compare_and_swap(&qimpl->queue_head,
		                                       NULL, element);
		if (prev_val == NULL) {
			prev_val = __sync_val_compare_and_swap(&qimpl->queue_tail,
			                                       NULL, element);
			if (prev_val == NULL) {
				// successfully added into an empty queue
				return;
			} else {
				// TODO think about this
				assert(0);
			}
		} else {
			curr_head = __sync_fetch_and_or(&qimpl->queue_head, 0);
			curr_tail = __sync_fetch_and_or(&qimpl->queue_tail, 0);
		}
	}
	do {
		qimpl->queue_tail->next = e_impl;
		qimpl->queue_tail = e_impl;
		return;
//		e_impl->next = curr_tail;
//		prev_val = __sync_val_compare_and_swap(&qimpl->queue_tail,
//		                                       curr_tail, e_impl);
//		if (prev_val == curr_tail) {
//			return;
//		}
//		curr_tail = prev_val;
	} while (true);
}

int lf_queue_dequeue(lf_queue_t *queue, lf_element_t **element)
{
	lf_queue_impl_t *qimpl = queue->queue;
	lf_element_impl_t *curr_head = __sync_fetch_and_or(&qimpl->queue_head, 0);
	lf_element_impl_t *curr_tail = __sync_fetch_and_or(&qimpl->queue_tail, 0);
	lf_element_impl_t *prev_val;

	if (curr_head == NULL) {
		return ENOMEM;
	}
	while (curr_head == curr_tail) {
		prev_val = __sync_val_compare_and_swap(&qimpl->queue_head,
		                                       curr_head, NULL);
		if (prev_val == curr_head) {
			// successfully deq the last element
			*element = &curr_head->elem;
			return 0;
		} else {
			curr_head = __sync_fetch_and_or(&qimpl->queue_head, 0);
			curr_tail = __sync_fetch_and_or(&qimpl->queue_tail, 0);
		}
	}

	do {
		prev_val = __sync_val_compare_and_swap(&qimpl->queue_head,
		                                       curr_head, curr_head->next);
		if (prev_val == curr_head) {
			curr_head->next = NULL;
			*element = &curr_head->elem;
			return 0;
		}
		curr_head = prev_val;
		if (curr_head == NULL) {
			return ENOMEM;
		}
	} while (true);
}

void lf_queue_put(lf_queue_t *queue, lf_element_t *element)
{
	lf_queue_impl_t *qimpl = queue->queue;
	lf_element_impl_t *element_impl = container_of(element, lf_element_impl_t, elem);
	lf_element_impl_t *curr_free = __sync_fetch_and_or(&qimpl->free_head, 0);
	lf_element_impl_t *prev_val = NULL;

	do {
		element_impl->next = curr_free;
		prev_val = __sync_val_compare_and_swap(&qimpl->free_head,
		                                       curr_free, element_impl);
		if (prev_val == curr_free) {
			// swap was successful
			break;
		}
		curr_free = prev_val;
	} while (true);
}

