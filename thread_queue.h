/* thread_queue.h -------------------------------------------------- */
#ifndef THREAD_QUEUE_H
#define THREAD_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include "uthreads.h"                 /* defines thread_t and MAX_THREAD_NUM */

/* You can override this with -DTHREAD_QUEUE_CAPACITY or a #define
   **before** including this header. */
#ifndef THREAD_QUEUE_CAPACITY
#define THREAD_QUEUE_CAPACITY MAX_THREAD_NUM   /* 100 by default */
#endif

typedef struct {
    size_t      head;                         /* index of next element to pop */
    size_t      tail;                         /* index of next free slot      */
    size_t      size;                         /* current number of elements   */
    thread_t   *data[THREAD_QUEUE_CAPACITY];  /* the storage itself           */
} thread_queue_t;

/* O(1) interface -------------------------------------------------- */
void  tq_init        (thread_queue_t *q);
bool  tq_is_empty    (const thread_queue_t *q);
bool  tq_is_full     (const thread_queue_t *q);
bool  tq_enqueue     (thread_queue_t *q, thread_t *thr);  /* false if full   */
bool  tq_dequeue     (thread_queue_t *q, thread_t **out); /* false if empty  */
bool  tq_peek        (const thread_queue_t *q, thread_t **out); /* inspect   */

#endif /* THREAD_QUEUE_H */