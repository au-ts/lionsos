/*
 * Copyright 2022, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sddf/util/fence.h>
#include <sddf/util/util.h>
#include <sddf/network/queue.h>
 
typedef struct fw_buff_desc {
    /* offset of buffer within buffer memory region or io address of buffer */
    uint64_t io_or_offset;
    /* length of data inside buffer */
    uint16_t len;
} fw_buff_desc_t;

typedef struct fw_queue {
    /* index to insert at */
    uint16_t tail;
    /* index to remove from */
    uint16_t head;
    /* buffer descripter array */
    fw_buff_desc_t buffers[];
} fw_queue_t;

typedef struct fw_queue_handle {
    fw_queue_t *queue;
    /* capacity of the queue */
    uint32_t capacity;
} fw_queue_handle_t;

 /**
 * Convert a firewall buffer descriptor to net.
 *
 * @param fw_desc firewall buffer descriptor.
 *
 * @return net buffer descriptor.
 */
static inline net_buff_desc_t fw_to_net_desc(fw_buff_desc_t fw_desc) {
   net_buff_desc_t net_desc = {fw_desc.io_or_offset, fw_desc.len};
   return net_desc;
}

 /**
 * Convert a net buffer descriptor to firewall.
 *
 * @param net_desc net buffer descriptor.
 *
 * @return firewall buffer descriptor.
 */
static inline fw_buff_desc_t net_fw_desc(net_buff_desc_t net_desc) {
   fw_buff_desc_t fw_desc = {net_desc.io_or_offset, net_desc.len};
   return fw_desc;
}

/**
 * Get the number of buffers enqueued into a queue.
 *
 * @param queue queue handle for the queue to get the length of.
 *
 * @return number of buffers enqueued into a queue.
 */
static inline uint16_t fw_queue_length(fw_queue_t *queue)
{
    return queue->tail - queue->head;
}

/**
 * Check if a queue is empty.
 *
 * @param queue queue handle to check.
 *
 * @return true indicates the queue is empty, false otherwise.
 */
static inline bool fw_queue_empty(fw_queue_handle_t *queue)
{
    return queue->queue->tail - queue->queue->head == 0;
}

/**
 * Check if a queue is full.
 *
 * @param queue queue handle to check.
 *
 * @return true indicates the queue is full, false otherwise.
 */
static inline bool fw_queue_full(fw_queue_handle_t *queue)
{
    return queue->queue->tail - queue->queue->head == queue->capacity;
}

/**
 * Enqueue an element into a queue.
 *
 * @param queue queue to enqueue into.
 * @param buffer buffer descriptor for buffer to be enqueued.
 *
 * @return -1 when queue is full, 0 on success.
 */
static inline int fw_enqueue(fw_queue_handle_t *queue, fw_buff_desc_t buffer)
{
    if (fw_queue_full(queue)) {
        return -1;
    }

    queue->queue->buffers[queue->queue->tail % queue->capacity] = buffer;
#ifdef CONFIG_ENABLE_SMP_SUPPORT
     THREAD_MEMORY_RELEASE();
#endif
    queue->queue->tail++;

    return 0;
}

/**
 * Dequeue an element from a queue.
 *
 * @param queue queue handle to dequeue from.
 * @param buffer pointer to buffer descriptor for buffer to be dequeued.
 *
 * @return -1 when queue is empty, 0 on success.
 */
static inline int fw_dequeue(fw_queue_handle_t *queue, fw_buff_desc_t *buffer)
{
    if (fw_queue_empty(queue)) {
        return -1;
    }

    *buffer = queue->queue->buffers[queue->queue->head % queue->capacity];
#ifdef CONFIG_ENABLE_SMP_SUPPORT
    THREAD_MEMORY_RELEASE();
#endif
    queue->queue->head++;

    return 0;
}

/**
 * Initialise the shared queue.
 *
 * @param queue_handle queue handle to use.
 * @param queue pointer to queue in shared memory.
 * @param capacity capacity of the queue.
 */
static inline void fw_queue_init(fw_queue_handle_t *queue_handle, fw_queue_t *queue, uint32_t capacity)
{
    queue_handle->queue = queue;
    queue_handle->capacity = capacity;
}
