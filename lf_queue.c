#include "lf_queue.h"

#include <malloc.h>
#include <errno.h>
#include <assert.h>
#include <stdbool.h>
#include <syscall.h>
#include <unistd.h>
#include <stdlib.h>

typedef struct lf_element_impl lf_element_impl_t;

typedef struct lf_element_impl {
    lf_element_t elem;
    lf_element_impl_t *next;
    // followed by the element data
} lf_element_impl_t;

typedef struct lf_queue_impl {
    lf_element_impl_t *elements;
    lf_element_impl_t **ptrs;
    size_t n_elements;
    size_t element_size;
    // TODO multiple free lists
    lf_element_impl_t *free_head;
    size_t head;
    size_t tail;
} lf_queue_impl_t;

static inline size_t raw_elem_size(size_t element_size)
{
	return element_size + sizeof(lf_element_impl_t);
}

pid_t get_tid(void)
{
	return (pid_t)syscall(SYS_gettid);;
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
	// we count on calloc setting the memory to zero
	qimpl->ptrs = calloc(n_elements, sizeof(lf_queue_impl_t*));
	qimpl->free_head = qimpl->elements;
	qimpl->head = 1;
	qimpl->tail = 0;

	assert(n_elements > 0);
	assert(element_size > 0);
	assert(queue != NULL);

//	printf("qimpl->elements=%p\n", qimpl->elements);
	curr = qimpl->elements;
	for (i = 0; i < n_elements - 1; ++i) {
		curr->next = (lf_element_impl_t *) ((char*)curr + raw_elem_size(element_size));
		curr->elem.data = (void*)&curr->next + sizeof(curr->next);
//		printf("curr=%p curr->next=%p data=%p diff=%d\n", curr, curr->next, curr->elem.data, (int)((char*)curr->next - (char*)curr));
		curr = curr->next;
	}
//	printf("qimpl->elements=%p\n", qimpl->elements);
	curr->next = NULL;
	curr->elem.data = (void*)&curr->next + sizeof(curr->next);
	queue->queue = qimpl;
	return 0;
}

void lf_queue_destroy(lf_queue_t *queue)
{
	lf_queue_impl_t *qimpl = queue->queue;

	assert(queue->queue != NULL);
	free(qimpl->elements);
	free(qimpl->ptrs);
	free(qimpl);
	queue->queue = NULL;
}

int lf_queue_get(lf_queue_t *queue, lf_element_t **element)
{
	lf_queue_impl_t *qimpl = queue->queue;
	lf_element_impl_t *curr_free = qimpl->free_head;
	lf_element_impl_t *prev_val;

	while (curr_free) {
//		printf("curr_free=%p curr_free->next=%p\n", curr_free, curr_free->next);
		prev_val = __sync_val_compare_and_swap(&qimpl->free_head,
		                                       curr_free, curr_free->next);
		if (prev_val == curr_free) {
			// swap was successful
			curr_free->next = NULL;
			*element = &curr_free->elem;
			printf("(%d) GET curr_free=%p *element=%p\n", get_tid(), curr_free, *element);
			return 0;
		}
		curr_free = prev_val;
	}
	return ENOMEM;
}

#define container_of(ptr, container, member) \
	((container *)((char *)ptr - offsetof(container, member)))

#define MAX_TID 100000

void lf_queue_enqueue(lf_queue_t *queue, lf_element_t *element)
{
	lf_queue_impl_t *qimpl = queue->queue;
	lf_element_impl_t *e_impl = container_of(element, lf_element_impl_t, elem);
	size_t tail;
	lf_element_impl_t **pptr;
	lf_element_impl_t *ptr;

	do {
		tail = __sync_add_and_fetch(&qimpl->tail, 0);
		pptr = &qimpl->ptrs[(tail + 1) % qimpl->n_elements];
		ptr = *pptr;
		printf("(%d) ENQ 1 pptr=%p head=%lu tail=%lu\n", get_tid(),
		       pptr, qimpl->head, tail);
		if ((int)ptr > MAX_TID) {
			continue;
		}
		if (__sync_bool_compare_and_swap(pptr, ptr, e_impl)) {
			tail = __sync_add_and_fetch(&qimpl->tail, 1);
			printf("(%d) ENQ 2 pptr=%p head=%lu tail=%lu\n", get_tid(),
			       pptr, qimpl->head, tail);
			return;
		}
	} while (true);
}

int lf_queue_dequeue(lf_queue_t *queue, lf_element_t **element)
{
	lf_queue_impl_t *qimpl = queue->queue;
	size_t head;
	size_t tail;
	lf_element_impl_t **ptr;
	lf_element_impl_t *curr_ptr;
	do {
		head = __sync_add_and_fetch(&qimpl->head, 0);
		tail = __sync_add_and_fetch(&qimpl->tail, 0);
		if (head > tail) {
			return ENOMEM;
		}
		ptr = &qimpl->ptrs[head % qimpl->n_elements];
		curr_ptr = *ptr;
		printf("(%d) DEQ 1 curr_ptr=%p head=%lu tail=%lu\n", get_tid(), curr_ptr, head, tail);
		if ((int)curr_ptr < MAX_TID) {
			// some other thread have already made the swap
			continue;
		}
		if (__sync_bool_compare_and_swap(ptr, curr_ptr, get_tid())) {
			head = __sync_add_and_fetch(&qimpl->head, 1);
			*element = &curr_ptr->elem;
			printf("(%d) DEQ 2 *element=%p curr_ptr=%p head=%lu tail=%lu\n",
			       get_tid(), *element, curr_ptr, head, tail);
			return 0;
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

