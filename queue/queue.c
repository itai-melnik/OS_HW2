#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h> // Required for sigjmp_buf

// Assuming these types are defined elsewhere based on your struct
// You might need to include relevant header files for these.
// For the purpose of this example, we'll just define a dummy thread_entry_point
typedef void (*thread_entry_point)(void);

typedef enum {
    THREAD_UNUSED = 0, /**< Slot is unused. */
    THREAD_READY,      /**< Thread is ready to run. */
    THREAD_RUNNING,    /**< Thread is currently executing. */
    THREAD_BLOCKED,    /**< Thread is blocked (explicitly or sleeping). */
    THREAD_TERMINATED  /**< Thread has finished execution (internal use only). */
} thread_state_t;

/**
 * @brief Thread Control Block (TCB)
 *
 * Each thread (except for the main thread) has its own allocated stack and context.
 * The TCB stores all metadata required for managing the thread.
 */
typedef struct {
    int tid;                    /**< Unique thread identifier. */
    thread_state_t state;       /**< Current thread state. */
    sigjmp_buf env;             /**< Jump buffer for context switching using sigsetjmp/siglongjmp. */
    int quantums;               /**< Count of quantums this thread has executed. */
    int sleep_until;            /**< Global quantum count until which the thread should sleep (0 if not sleeping). */
    thread_entry_point entry;   /**< Entry point function for the thread. */
} thread_t;


// Structure for a node in the linked list
typedef struct QueueNode {
    thread_t *thread_ptr;      // Pointer to the thread_t structure
    struct QueueNode *next;    // Pointer to the next node
} QueueNode;

// Structure for the Queue
typedef struct {
    QueueNode *front;          // Pointer to the front of the queue
    QueueNode *rear;           // Pointer to the rear of the queue
} Queue;

/**
 * @brief Initializes an empty queue.
 * @return A pointer to the newly created queue, or NULL if allocation fails.
 */
Queue* create_queue() {
    Queue *q = (Queue*)malloc(sizeof(Queue));
    if (q == NULL) {
        perror("Failed to allocate memory for queue");
        return NULL;
    }
    q->front = q->rear = NULL;
    return q;
}

/**
 * @brief Checks if the queue is empty.
 * @param q Pointer to the queue.
 * @return 1 if the queue is empty, 0 otherwise.
 */
int is_empty(Queue *q) {
    return (q == NULL || q->front == NULL);
}

/**
 * @brief Adds a thread pointer to the rear of the queue.
 * @param q Pointer to the queue.
 * @param thread_ptr Pointer to the thread_t structure to enqueue.
 * @return 0 on success, -1 on failure (e.g., memory allocation failure).
 */
int enqueue(Queue *q, thread_t *thread_ptr) {
    if (q == NULL) {
        fprintf(stderr, "Error: Queue is not initialized.\n");
        return -1;
    }

    QueueNode *new_node = (QueueNode*)malloc(sizeof(QueueNode));
    if (new_node == NULL) {
        perror("Failed to allocate memory for queue node");
        return -1;
    }

    new_node->thread_ptr = thread_ptr;
    new_node->next = NULL;

    if (q->rear == NULL) { // Queue is empty
        q->front = q->rear = new_node;
    } else {
        q->rear->next = new_node;
        q->rear = new_node;
    }
    return 1;
}

/**
 * @brief Removes and returns the thread pointer from the front of the queue.
 * @param q Pointer to the queue.
 * @return A pointer to the thread_t structure, or NULL if the queue is empty.
 */
thread_t* dequeue(Queue *q) {
    if (is_empty(q)) {
        return NULL; // Queue is empty
    }

    QueueNode *temp = q->front;
    thread_t *thread_ptr = temp->thread_ptr;

    q->front = q->front->next;

    if (q->front == NULL) { // If the queue becomes empty after dequeuing
        q->rear = NULL;
    }

    free(temp); // Free the dequeued node

    return thread_ptr;
}

// /**
//  * @brief Returns the thread pointer at the front of the queue without removing it.
//  * @param q Pointer to the queue.
//  * @return A pointer to the thread_t structure, or NULL if the queue is empty.
//  */
// thread_t* peek(Queue *q) {
//     if (is_empty(q)) {
//         return NULL; // Queue is empty
//     }
//     return q->front->thread_ptr;
// }

/**
 * @brief Frees all nodes in the queue and the queue structure itself.
 * @param q Pointer to the queue.
 */
void destroy_queue(Queue *q) {
    if (q == NULL) {
        return;
    }
    QueueNode *current = q->front;
    QueueNode *next;
    while (current != NULL) {
        next = current->next;
        // We don't free the thread_t pointers here, as they are managed
        // elsewhere. We only free the queue nodes.
        free(current);
        current = next;
    }
    free(q);
}