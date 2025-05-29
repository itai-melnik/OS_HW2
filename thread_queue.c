/* thread_queue.c -------------------------------------------------- */
#include "thread_queue.h"

static inline size_t next_index(size_t i) {
    return (i + 1) % THREAD_QUEUE_CAPACITY;   /* wrap-around helper */
}

void tq_init(thread_queue_t *q) {
    q->head = q->tail = q->size = 0;
}

bool tq_is_empty(const thread_queue_t *q) { return q->size == 0; }
bool tq_is_full (const thread_queue_t *q) { return q->size == THREAD_QUEUE_CAPACITY; }

bool tq_enqueue(thread_queue_t *q, thread_t *thr) {
    if (tq_is_full(q)) return false;
    q->data[q->tail] = thr;
    q->tail = next_index(q->tail);
    q->size++;
    return true;
}

bool tq_dequeue(thread_queue_t *q, thread_t **out) {
    if (tq_is_empty(q)) return false;
    if (out) *out = q->data[q->head];
    q->head = next_index(q->head);
    q->size--;
    return true;
}

bool tq_peek(const thread_queue_t *q, thread_t **out) {
    if (tq_is_empty(q)) return false;
    if (out) *out = q->data[q->head];
    return true;
}