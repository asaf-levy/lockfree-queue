#include "lf_queue.h"

#include <malloc.h>
#include <errno.h>
#include <assert.h>
#include <stdbool.h>
#include <syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct lf_element_impl lf_element_impl_t;

typedef struct lf_element_impl {
    lf_element_t elem;
    uint32_t mod_count;
    size_t next_desc;
    // followed by the element data
} lf_element_impl_t;

typedef struct lf_queue_impl {
    lf_element_impl_t *elements;
    lf_element_impl_t **ptrs;
    size_t n_elements;
    size_t element_size;
    // TODO multiple free lists
    size_t free_head;
    size_t head;
    size_t tail;
} lf_queue_impl_t;

__thread pid_t g_tid = 0;
pid_t get_tid(void)
{
	if (!g_tid) {
		g_tid = (pid_t)syscall(SYS_gettid);
	}
	return g_tid;
}

#if 0
#define PRINT(FMT, ARGS...)  \
	printf(FMT, ##ARGS)
#else
#define PRINT(FMT, ARGS...)
#endif

#define container_of(ptr, container, member) \
	((container *)((char *)ptr - offsetof(container, member)))
#define USED_BIT                0x8000000000000000
#define ELEMENT_OFFSET_MASK     0x00000000ffffffff
#define QUEUE_GEN_MASK          0x7fffffff00000000

static inline size_t raw_elem_size(size_t element_size)
{
	return element_size + sizeof(lf_element_impl_t);
}

static inline size_t elem_offset(lf_queue_impl_t *queue, lf_element_impl_t *element)
{
	return (((void*)element - (void*)queue->elements)) / raw_elem_size(queue->element_size);;
}

static inline lf_element_impl_t *get_elem_by_offset(lf_queue_impl_t *queue,
                                                    size_t element_offset)
{
	return (lf_element_impl_t *) ((void*)queue->elements +
	                              (element_offset * raw_elem_size(queue->element_size)));
}

static inline size_t get_element_descriptor(lf_queue_impl_t *queue,
                                            lf_element_impl_t *element)
{
	size_t queue_gen = (queue->tail + 1) / queue->n_elements;
	size_t element_descriptor = queue_gen;
	element_descriptor = (element_descriptor << 32) & QUEUE_GEN_MASK;
	return element_descriptor | elem_offset(queue, element);
}

static inline lf_element_impl_t *get_element_by_descriptor(lf_queue_impl_t *queue,
                                                           size_t element_descriptor)
{
	return get_elem_by_offset(queue, element_descriptor & ELEMENT_OFFSET_MASK);
}

static inline size_t get_free_list_descriptor(lf_queue_impl_t *queue,
                                              lf_element_impl_t *element)
{
	size_t mod_count = element->mod_count;
	size_t element_descriptor = mod_count << 32;
	return element_descriptor | elem_offset(queue, element);
}

int lf_queue_init(lf_queue_handle_t *queue, size_t n_elements, size_t element_size)
{
	// TODO malloc checks
	// TODO max queue size is 2^32
	size_t i;
	lf_element_impl_t *curr;
	lf_element_impl_t *next;
	lf_queue_impl_t *qimpl = malloc(sizeof(lf_queue_impl_t));
	qimpl->n_elements = n_elements;
	qimpl->element_size = element_size;
	qimpl->elements = calloc(n_elements, raw_elem_size(element_size));
	// we count on calloc setting the memory to zero
	qimpl->ptrs = calloc(n_elements, sizeof(lf_queue_impl_t*));
	qimpl->head = 1;
	qimpl->tail = 0;

	if (n_elements == 0 || element_size == 0 || queue == NULL) {
		return EINVAL;
	}

	curr = qimpl->elements;
	for (i = 0; i < n_elements - 1; ++i) {
		curr->mod_count = 1;
		next = (lf_element_impl_t *)((char*)curr + raw_elem_size(element_size));
		curr->next_desc = get_free_list_descriptor(qimpl, next);
		curr->elem.data = (void*)&curr->next_desc + sizeof(curr->next_desc);
		curr = next;
	}
	curr->mod_count = 0;
	curr->next_desc = 0;
	curr->elem.data = (void*)&curr->next_desc + sizeof(curr->next_desc);

	qimpl->free_head = get_free_list_descriptor(qimpl, qimpl->elements);
	*queue = (lf_queue_handle_t)qimpl;
	return 0;
}

void lf_queue_destroy(lf_queue_handle_t queue)
{
	lf_queue_impl_t *qimpl = (lf_queue_impl_t *)queue;;
	free(qimpl->elements);
	free(qimpl->ptrs);
	free(qimpl);
}

int lf_queue_get(lf_queue_handle_t queue, lf_element_t **element)
{
	lf_queue_impl_t *qimpl = (lf_queue_impl_t *)queue;
	size_t curr_free = qimpl->free_head;
	size_t prev_val;

	while (curr_free != 0) {
		lf_element_impl_t *eimpl = get_element_by_descriptor(qimpl, curr_free);
		prev_val = __sync_val_compare_and_swap(&qimpl->free_head,
		                                       curr_free, eimpl->next_desc);
		if (prev_val == curr_free) {
			// swap was successful
			eimpl->next_desc = 0;
			eimpl->mod_count = (eimpl->mod_count + 1) % ELEMENT_OFFSET_MASK;
			*element = &eimpl->elem;
			return 0;
		}
		curr_free = prev_val;
	}
	return ENOMEM;
}

void lf_queue_put(lf_queue_handle_t queue, lf_element_t *element)
{
	lf_queue_impl_t *qimpl = (lf_queue_impl_t *)queue;
	lf_element_impl_t *element_impl = container_of(element, lf_element_impl_t, elem);
	size_t curr_free = qimpl->free_head;
	size_t prev_val;

	do {
		element_impl->next_desc = curr_free;
		prev_val = __sync_val_compare_and_swap(&qimpl->free_head,
		                                       curr_free, get_free_list_descriptor(qimpl, element_impl));
		if (prev_val == curr_free) {
//			PRINT("(%d) PUT curr_free=%p element=%p\n", get_tid(), curr_free, element);
			break;
		}
		curr_free = prev_val;
	} while (true);
}

void lf_queue_enqueue(lf_queue_handle_t queue, lf_element_t *element)
{
	lf_queue_impl_t *qimpl = (lf_queue_impl_t *)queue;
	lf_element_impl_t *e_impl = container_of(element, lf_element_impl_t, elem);
//	size_t prev_head;
//	size_t prev_tail;
	size_t tail;
	lf_element_impl_t **pptr;
	size_t elem_desc;

	do {
		tail = __sync_add_and_fetch(&qimpl->tail, 0);
//		tail = qimpl->tail;
//		prev_tail = tail;
		pptr = &qimpl->ptrs[(tail + 1) % qimpl->n_elements];
		elem_desc = (size_t)*pptr;
//		prev_head = qimpl->head;
//		PRINT("(%d) ENQ 1 pptr=%p element_descriptor=%lx head=%lu tail=%lu\n", get_tid(),
//		       pptr, elem_desc, qimpl->head, tail);
		if ((elem_desc & USED_BIT)) {
			// this slot is already in use, some other thread won the
			// race to enqueue it
			PRINT("(%d) ENQ SLOT-TAKEN pptr=%p elem_desc=%lx head=%lu tail=%lu\n", get_tid(),
			       pptr, elem_desc, qimpl->head, tail);
			continue;
		}
		if ((elem_desc & ~USED_BIT) && ((elem_desc & ~USED_BIT) > tail)) {
			// this slot is free, but the value this thread think tail
			// has is outdated meaning that some other thread(s) have
			// already enqueued and dequeued this slot since we got
			// the value of tail
			PRINT("(%d) ENQ TAIL-UPDATED pptr=%p head=%lu tail=%lu\n", get_tid(),
			       pptr, qimpl->head, tail);
			continue;
		}
		if (__sync_bool_compare_and_swap(pptr, elem_desc,
		                                 USED_BIT | get_element_descriptor(qimpl, e_impl))) {
			// one could claim since tail always advances there is the
			// risk of a wraparound. However this is not a practical
			// concern since even if you add a new item to the queue
			// every nano sec it would still take ~584 years for 64 bits
			// to wrap.
			tail = __sync_add_and_fetch(&qimpl->tail, 1);
//			PRINT("(%d) ENQ 2 pptr=%p element_descriptor=%lu calc_element_descriptor=%lx prev_head=%lu head=%lu prev_tail=%lu tail=%lu\n", get_tid(),
//			       pptr, elem_desc,
//			       get_element_descriptor(qimpl, e_impl), prev_head, qimpl->head, prev_tail, tail);
			return;
		} else {
			PRINT("(%d) ENQ RETRY pptr=%p head=%lu tail=%lu\n", get_tid(),
			       pptr, qimpl->head, tail);
		}
	} while (true);
}

int lf_queue_dequeue(lf_queue_handle_t queue, lf_element_t **element)
{
	lf_queue_impl_t *qimpl = (lf_queue_impl_t *)queue;
	size_t head;
	size_t tail;
	size_t elem_desc;
	int64_t desc_queue_gen;
	int64_t current_queue_gen;
	lf_element_impl_t **ptr;
	do {
		head = __sync_add_and_fetch(&qimpl->head, 0);
		tail = __sync_add_and_fetch(&qimpl->tail, 0);
		if (head > tail) {
			return ENOMEM;
		}
		ptr = &qimpl->ptrs[head % qimpl->n_elements];
		elem_desc = (size_t)*ptr;
		desc_queue_gen = (int64_t) ((elem_desc & QUEUE_GEN_MASK) >> 32);
		current_queue_gen = (int64_t) ((head / qimpl->n_elements) & 0xffffffff);
		if (desc_queue_gen - current_queue_gen > 0 && desc_queue_gen - current_queue_gen < 0x0fffff) {
			PRINT("(%d) DEQ OUTDATED QUEUE GEN, desc_queue_gen=%lu current_queue_gen=%lu element_descriptor=0x%lx head=%lu tail=%lu\n", get_tid(),
			      desc_queue_gen, current_queue_gen, elem_desc, head, tail);
			// some other thread have already dequeued this element
			continue;
		}
//		PRINT("(%d) DEQ 1 element_descriptor=%lx head=%lu tail=%lu\n", get_tid(),
//		       elem_desc, head, tail);
		if ((elem_desc & USED_BIT) == 0) {
			PRINT("(%d) DEQ USED_BIT NOT SET, element_descriptor=%lu head=%lu tail=%lu\n", get_tid(),
			       elem_desc, head, tail);
			// some other thread have already dequeued this element
			continue;
		}
		if (__sync_bool_compare_and_swap(ptr, elem_desc, tail)) {
			__sync_add_and_fetch(&qimpl->head, 1);
			*element = &get_element_by_descriptor(qimpl, elem_desc & ~USED_BIT)->elem;
//			PRINT("(%d) DEQ 2 *element=%p element_descriptor=%lx desc_queue_gen=%lu prev_head=%lu head=%lu tail=%lu\n",
//			      get_tid(), *element, elem_desc, desc_queue_gen, prev_head, head, tail);
			return 0;
		} else {
			PRINT("(%d) DEQ RETRY element_descriptor=%lx head=%lu tail=%lu\n", get_tid(),
			       elem_desc, head, tail);
		}
	} while (true);
}

