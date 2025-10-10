#define QUEUE_CAPACITY BUFSIZE

struct char_queue {
    /* index to insert at */
    uint32_t tail;
    /* index to remove from */
    uint32_t head;
    /* data */
    char buf[QUEUE_CAPACITY];
};

typedef struct char_queue char_queue_t;

/**
 * Return the number of bytes of data stored in the queue.
 *
 * @param queue_handle queue containing the data.
 *
 * @return The number bytes of data stored in the queue.
 */
static inline uint32_t char_queue_length(char_queue_t *queue_handle)
{
    return queue_handle->tail - queue_handle->head;
}

/**
 * Check if the queue is empty.
 *
 * @param queue_handle queue to check.
 * @param local_head head which points to the next character to be dequeued.
 *
 * @return true indicates the queue is empty, false otherwise.
 */
static inline int char_queue_empty(char_queue_t *queue_handle, uint32_t local_head)
{
    return local_head == queue_handle->tail;
}

/**
 * Check if the queue is full.
 *
 * @param queue_handle queue to check.
 * @param local_tail tail which points to the next enqueue slot.
 *
 * @return true indicates the queue is full, false otherwise.
 */
static inline int char_queue_full(char_queue_t *queue_handle, uint32_t local_tail)
{
    return local_tail - queue_handle->head == QUEUE_CAPACITY;
}

/**
 * Enqueue a character into a queue. Update the shared tail so the character
 * is visible to the consumer.
 *
 * @param queue_handle queue to enqueue into.
 * @param character character to be enqueued.
 *
 * @return -1 when queue is full, 0 on success.
 */
static inline int char_enqueue(char_queue_t *queue_handle, char character)
{
    uint32_t *tail = &queue_handle->tail;

    if (char_queue_full(queue_handle, *tail)) {
        return -1;
    }

    queue_handle->buf[*tail % QUEUE_CAPACITY] = character;
    (*tail)++;

    return 0;
}

/**
 * Enqueue a character locally into a queue. Update a local tail variable so the
 * character is not visible to the consumer.
 *
 * @param queue_handle queue to enqueue into.
 * @param local_tail address of the tail to be used and incremented.
 * @param character character to be enqueued.
 *
 * @return -1 when queue is full, 0 on success.
 */
static inline int char_enqueue_local(char_queue_t *queue_handle, uint32_t *local_tail, char character)
{
    if (char_queue_full(queue_handle, *local_tail)) {
        return -1;
    }

    queue_handle->buf[*local_tail % QUEUE_CAPACITY] = character;
    (*local_tail)++;

    return 0;
}

/**
 * Dequeue a character from a queue. Update the shared head so the removal of the
 * character is visible to the producer.
 *
 * @param queue_handle queue to dequeue from.
 * @param character address of character to copy into.
 *
 * @return -1 when queue is empty, 0 on success.
 */
static inline int char_dequeue(char_queue_t *queue_handle, char *character)
{
    uint32_t *head = &queue_handle->head;

    if (char_queue_empty(queue_handle, *head)) {
        return -1;
    }

    *character = queue_handle->buf[*head % QUEUE_CAPACITY];
    (*head)++;

    return 0;
}

/**
 * Dequeue a character locally from a queue. Update a local head variable so the
 * removal of the character is not visible to the producer.
 *
 * @param queue_handle queue to dequeue from.
 * @param local_head address of the head to be used and incremented.
 * @param character character to copy into.
 *
 * @return -1 when queue is empty, 0 on success.
 */
static inline int char_dequeue_local(char_queue_t *queue_handle, uint32_t *local_head, char *character)
{
    if (char_queue_empty(queue_handle, *local_head)) {
        return -1;
    }

    *character = queue_handle->buf[*local_head % QUEUE_CAPACITY];
    (*local_head)++;

    return 0;
}

/**
 * Return the number of free bytes remaining in the queue. This is the number of
 * bytes that can be enqueued until the queue is full.
 *
 * @param queue_handle queue to be filled with data.
 *
 * @return The amount of free space remaining in the queue.
 */
static inline uint32_t char_queue_free(char_queue_t *queue_handle)
{
    return QUEUE_CAPACITY - char_queue_length(queue_handle);
}


/**
 * Return the number of bytes that can be copied into the queue contiguously. This
 * is the number of bytes that can be copied into the queue with a single call of memcpy.
 *
 * @param queue_handle queue to be filled with data.
 *
 * @return The amount of contiguous free space remaining in the queue.
 */
static inline uint32_t char_queue_contiguous_free(char_queue_t *queue_handle)
{
    return MIN(QUEUE_CAPACITY - (queue_handle->tail % QUEUE_CAPACITY),
               char_queue_free(queue_handle));
}

/**
 * Update the value of the tail in the shared data structure to make
 * locally enqueued data visible.
 *
 * @param queue_handle queue to update.
 * @param local_tail tail which points to the last character enqueued.
 */
static inline void char_queue_update_shared_tail(char_queue_t *queue_handle, uint32_t local_tail)
{
    uint32_t current_length = char_queue_length(queue_handle);
    uint32_t new_length = local_tail - queue_handle->head;

    /* Ensure updates to tail don't overwrite existing data */
    assert(new_length >= current_length);

    /* Ensure updates to tail don't exceed capacity restraints */
    assert(new_length <= QUEUE_CAPACITY);

#ifdef CONFIG_ENABLE_SMP_SUPPORT
    THREAD_MEMORY_RELEASE();
#endif

    queue_handle->tail = local_tail;
}


/**
 * Enqueue a buffer of contiguous characters into a queue.
 *
 * @param queue_handle queue to be filled with data.
 * @param num number of characters to enqueue.
 * @param src pointer to characters to be transferred.
 *
 * @return Number of characters actually enqueued.
 */
static inline uint32_t char_queue_enqueue_batch(char_queue_t *queue_handle, uint32_t num, const char *src)
{
    char *dst = queue_handle->buf + (queue_handle->tail % QUEUE_CAPACITY);
    uint32_t avail = char_queue_free(queue_handle);
    uint32_t num_prewrap;
    uint32_t num_postwrap;

    num = MIN(num, avail);
    num_prewrap = char_queue_contiguous_free(queue_handle);
    num_prewrap = MIN(num, num_prewrap);
    num_postwrap = num - num_prewrap;

    sddf_memcpy(dst, src, num_prewrap);
    if (num_postwrap) {
        sddf_memcpy(queue_handle->buf, src + num_prewrap, num_postwrap);
    }

    char_queue_update_shared_tail(queue_handle, queue_handle->tail + num);

    return num;
}


