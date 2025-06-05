#include "thread_queue.h"

/* helper for wrap-around */
static inline size_t next_index(size_t i) { return (i + 1) % QUEUE_CAPACITY; }

/* O(1) operations ------------------------------------------------- */
void queue_init(int_queue_t *q) { q->head = q->tail = q->size = 0; }

int queue_is_empty(const int_queue_t *q) { return q->size == 0; }
int queue_is_full (const int_queue_t *q) { return q->size == QUEUE_CAPACITY; }

int queue_enqueue(int_queue_t *q, int value)
{
    if (queue_is_full(q)) return 0;
    q->data[q->tail] = value;
    q->tail = next_index(q->tail);
    q->size++;
    return 1;
}

int queue_dequeue(int_queue_t *q, int *out)
{
    if (queue_is_empty(q)) return 0;
    if (out) *out = q->data[q->head];
    q->head = next_index(q->head);
    q->size--;
    return 1;
}

int queue_peek(const int_queue_t *q, int *out)
{
    if (queue_is_empty(q)) return 0;
    if (out) *out = q->data[q->head];
    return 1;
}


/* helper to move one slot backwards */
static inline size_t prev_index(size_t i)
{
    return (i + QUEUE_CAPACITY - 1) % QUEUE_CAPACITY;
}

/* --------------------------------------------------------------- *
 * Remove first matching value.  Shifts elements toward head if the
 * match is in the middle.  Complexity O(n) with at most size-1
 * moves; no dynamic memory is touched.                            */
int queue_delete(int_queue_t *q, int value)
{
    if (queue_is_empty(q))
        return 0;                              /* nothing to do */

    /* 1. search for the value */
    size_t idx   = q->head;
    size_t count = 0;
    while (count < q->size && q->data[idx] != value) {
        idx = next_index(idx);
        count++;
    }
    if (count == q->size)
        return 0;                              /* not found */

    /* 2. found at idx ------------------------------------------ */
    if (idx == q->head) {                      /* deleting head  */
        q->head = next_index(q->head);
    }
    else if (idx == prev_index(q->tail)) {     /* deleting tail-1 */
        q->tail = prev_index(q->tail);
    }
    else {                                    /* deleting middle */
        size_t nxt = next_index(idx);
        while (nxt != q->tail) {               /* shift left     */
            q->data[idx] = q->data[nxt];
            idx = nxt;
            nxt = next_index(nxt);
        }
        q->tail = prev_index(q->tail);
    }

    q->size--;
    return 1;                                  /* success */
}