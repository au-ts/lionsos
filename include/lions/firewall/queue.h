/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sddf/util/fence.h>
#include <sddf/util/util.h>

typedef struct fw_queue_indeces {
    /* index to insert at */
    uint64_t tail;
    /* index to remove from */
    uint64_t head;
} fw_queue_indeces_t;

typedef struct fw_queue {
    /* shared indeces for queue */
    fw_queue_indeces_t *idx;
    /* shared data entries */
    uintptr_t entries;
    /* size of each data entry */
    size_t entry_size;
    /* capacity of the queue. Must be a power of 2 */
    size_t capacity;
} fw_queue_t;

/**
 * Get the number of valid entries in the queue.
 *
 * @param queue queue.
 *
 * @return number of valid entries.
 */
static inline uint16_t fw_queue_length(fw_queue_t *queue)
{
    return queue->idx->tail - queue->idx->head;
}

/**
 * Check if a queue is empty.
 *
 * @param queue queue.
 *
 * @return true indicates the queue is empty, false otherwise.
 */
static inline bool fw_queue_empty(fw_queue_t *queue)
{
    return queue->idx->tail - queue->idx->head == 0;
}

/**
 * Check if a queue is full.
 *
 * @param queue queue.
 *
 * @return true indicates the queue is full, false otherwise.
 */
static inline bool fw_queue_full(fw_queue_t *queue)
{
    return queue->idx->tail - queue->idx->head == queue->capacity;
}

/**
 * Enqueue an element into a queue.
 *
 * @param queue queue to enqueue into.
 * @param entry element to be enqueued.
 *
 * @return -1 when queue is full, 0 on success.
 */
static inline int fw_enqueue(fw_queue_t *queue,
                             void *entry)
{
    if (fw_queue_full(queue)) {
        return -1;
    }

    size_t offset = (queue->idx->tail % queue->capacity) * queue->entry_size;
    uintptr_t dest = queue->entries + offset;
    memcpy((void *)dest, entry, queue->entry_size);

#ifdef CONFIG_ENABLE_SMP_SUPPORT
     THREAD_MEMORY_RELEASE();
#endif
    queue->idx->tail++;

    return 0;
}

/**
 * Dequeue an element from a queue.
 *
 * @param queue queue to dequeue from.
 * @param entry address to copy dequeued entry to.
 *
 * @return -1 when queue is empty, 0 on success.
 */
static inline int fw_dequeue(fw_queue_t *queue,
                             void *entry)
{
    if (fw_queue_empty(queue)) {
        return -1;
    }

    size_t offset = (queue->idx->head % queue->capacity) * queue->entry_size;
    uintptr_t src = queue->entries + offset;
    memcpy(entry, (void *)src, queue->entry_size);

#ifdef CONFIG_ENABLE_SMP_SUPPORT
    THREAD_MEMORY_RELEASE();
#endif
    queue->idx->head++;

    return 0;
}

/**
 * Initialise the shared queue.
 *
 * @param queue address of queue to initialise.
 * @param data adress of shared data.
 * @param entry_size size of queue entries.
 * @param capacity capacity of the queue.
 */
static inline void fw_queue_init(fw_queue_t *queue,
                                 void *data,
                                 size_t entry_size,
                                 size_t capacity)
{
    queue->idx = (fw_queue_indeces_t *)data;
    queue->entries = (uintptr_t)data + sizeof(fw_queue_indeces_t);
    queue->entry_size = entry_size;
    queue->capacity = capacity;
}
