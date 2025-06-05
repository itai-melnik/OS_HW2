#ifndef THREAD_QUEUE_H
#define THREAD_QUEUE_H

#include <stddef.h>

/* Default capacity (can be overridden with -DQUEUE_CAPACITY=… at compile time). */
#ifndef QUEUE_CAPACITY
#define QUEUE_CAPACITY 128
#endif

typedef struct {
    size_t head;                  /* index of next element to dequeue */
    size_t tail;                  /* index of next free slot to enqueue */
    size_t size;                  /* current number of stored items   */
    int    data[QUEUE_CAPACITY];  /* storage                          */
} int_queue_t;

/* Public interface ------------------------------------------------ */
void queue_init   (int_queue_t *q);
int  queue_is_empty(const int_queue_t *q);          /* returns 1/0 */
int  queue_is_full (const int_queue_t *q);          /* returns 1/0 */
int  queue_enqueue (int_queue_t *q, int value);     /* returns 1 on success, 0 if full */
int  queue_dequeue (int_queue_t *q, int *out);      /* returns 1 on success, 0 if empty */
int  queue_peek    (const int_queue_t *q, int *out);/* returns 1 on success, 0 if empty */
int queue_delete(int_queue_t *q, int value);

#endif /* INT_QUEUE_H */