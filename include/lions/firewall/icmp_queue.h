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
#include <lions/firewall/protocols.h>

typedef struct icmp_req {
    uint32_t ip;
    uint8_t mac[ETH_HWADDR_LEN];
    uint8_t type;
    uint8_t code;
    // @kwinter: Extend this
    ipv4_packet_t old_hdr;
    uint64_t old_data;
} icmp_req_t;

typedef struct icmp_queue {
    /* index to insert at */
    uint16_t tail;
    /* index to remove from */
    uint16_t head;
    /* req array */
    icmp_req_t reqs[];
} icmp_queue_t;

typedef struct icmp_queue_handle {
    icmp_queue_t *queue;
    /* capacity of the queue */
    uint32_t capacity;
} icmp_queue_handle_t;

/**
 * Get the number of reqs enqueued into a queue.
 *
 * @param queue queue handle for the queue to get the length of.
 *
 * @return number of reqs enqueued into a queue.
 */
static inline uint16_t icmp_queue_length(icmp_queue_t *queue)
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
static inline bool icmp_queue_empty(icmp_queue_handle_t *queue)
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
static inline bool icmp_queue_full(icmp_queue_handle_t *queue)
{
    return queue->queue->tail - queue->queue->head == queue->capacity;
}

/**
 * Enqueue an element into a queue.
 *
 * @param queue queue to enqueue into.
 * @param req req to be enqueued.
 *
 * @return -1 when queue is full, 0 on success.
 */
static inline int icmp_enqueue(icmp_queue_handle_t *queue, icmp_req_t req)
{
    if (icmp_queue_full(queue)) {
        return -1;
    }

    queue->queue->reqs[queue->queue->tail % queue->capacity] = req;
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
 * @param req pointer to req to be dequeued.
 *
 * @return -1 when queue is empty, 0 on success.
 */
static inline int icmp_dequeue(icmp_queue_handle_t *queue, icmp_req_t *req)
{
    if (icmp_queue_empty(queue)) {
        return -1;
    }

    *req = queue->queue->reqs[queue->queue->head % queue->capacity];
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
static inline void icmp_queue_init(icmp_queue_handle_t *queue_handle, icmp_queue_t *queue, uint32_t capacity)
{
    queue_handle->queue = queue;
    queue_handle->capacity = capacity;
}
