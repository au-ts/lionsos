#pragma once

#include <stdint.h>
#include <string.h>
#include <protocols.h>

#define MAX_ARP_ENTRIES 512
#define ARP_BUFFER_SIZE 128

typedef struct arp_entry {
    uint8_t mac_addr[ETH_HWADDR_LEN];
    /* @kwinter: Add a timeout for stale ARP entries*/
    bool valid;
} arp_entry_t;

typedef struct arp_request {
    uint32_t ip_addr;
    uint8_t mac_addr[ETH_HWADDR_LEN];
    /* If valid is false on reply, drop the packet. */
    bool valid;
} arp_request_t;

typedef struct arp_queue {
    /* index to insert at */
    uint16_t tail;
    /* index to remove from */
    uint16_t head;
   /* arp array */
    arp_request_t queue[MAX_ARP_ENTRIES];
} arp_queue_t;

typedef struct arp_queue_handle {
    /* arp requests */
    arp_queue_t request;
    /* responses to arp requests */
    arp_queue_t response;
    /* capacity of the queues */
    uint32_t capacity;
} arp_queue_handle_t;

/**
 * Get the number of requests/responses enqueued into a queue.
 *
 * @param queue queue handle for the queue to get the length of.
 *
 * @return number of queue enqueued into a queue.
 */
static inline uint16_t arp_queue_length(arp_queue_t *queue)
{
    return queue->tail - queue->head;
}

/**
 * Check if the request queue is empty.
 *
 * @param queue queue handle for the request queue to check.
 *
 * @return true indicates the queue is empty, false otherwise.
 */
static inline bool arp_queue_empty_request(arp_queue_handle_t *queue)
{
    return queue->request.tail - queue->request.head == 0;
}

/**
 * Check if the response queue is empty.
 *
 * @param queue queue handle for the response queue to check.
 *
 * @return true indicates the queue is empty, false otherwise.
 */
static inline bool arp_queue_empty_response(arp_queue_handle_t *queue)
{
    return queue->response.tail - queue->response.head == 0;
}

/**
 * Check if the request queue is full.
 *
 * @param queue queue handle for the request queue to check.
 *
 * @return true indicates the queue is full, false otherwise.
 */
static inline bool arp_queue_full_request(arp_queue_handle_t *queue)
{
    return queue->request.tail - queue->request.head == queue->capacity;
}

/**
 * Check if the response queue is full.
 *
 * @param queue queue handle for the response queue to check.
 *
 * @return true indicates the queue is full, false otherwise.
 */
static inline bool arp_queue_full_response(arp_queue_handle_t *queue)
{
    return queue->response.tail - queue->response.head == queue->capacity;
}

/**
 * Enqueue an element into a request queue.
 *
 * @param queue queue to enqueue into.
 * @param ip_addr ip address of the request.
 *
 * @return -1 when queue is full, 0 on success.
 */
static inline int arp_enqueue_request(arp_queue_handle_t *queue, uint32_t ip_addr)
{
    if (arp_queue_full_request(queue)) {
        return -1;
    }

    queue->request.queue[queue->request.tail % queue->capacity].valid = true;
    queue->request.queue[queue->request.tail % queue->capacity].ip_addr = ip_addr;
    queue->request.tail++;

    return 0;
}

/**
 * Enqueue an element into an response queue.
 *
 * @param queue queue to enqueue into.
 * @param ip_addr ip address of the response.
 * @param mac_addr mac address of the response.
 * @param valid validity of the response.
 *
 * @return -1 when queue is full, 0 on success.
 */
static inline int arp_enqueue_response(arp_queue_handle_t *queue, uint32_t ip_addr, uint8_t mac_addr[ETH_HWADDR_LEN], bool valid)
{
    if (arp_queue_full_response(queue)) {
        return -1;
    }

    queue->response.queue[queue->response.tail % queue->capacity].ip_addr = ip_addr;
    queue->response.queue[queue->response.tail % queue->capacity].valid = valid;
    memcpy(&queue->response.queue[queue->response.tail % queue->capacity].mac_addr, mac_addr, ETH_HWADDR_LEN);
    queue->response.tail++;

    return 0;
}

/**
 * Dequeue an element from the request queue.
 *
 * @param queue queue handle to dequeue from.
 * @param request pointer to request to be dequeued.
 *
 * @return -1 when queue is empty, 0 on success.
 */
static inline int arp_dequeue_request(arp_queue_handle_t *queue, arp_request_t *request)
{
    if (arp_queue_empty_request(queue)) {
        return -1;
    }

    memcpy(request, &queue->request.queue[queue->request.head % queue->capacity], sizeof(arp_request_t));

    queue->request.queue[queue->request.head % queue->capacity].valid = false;
    queue->request.head++;

    return 0;
}

/**
 * Dequeue an element from the response queue.
 *
 * @param queue queue handle to dequeue from.
 * @param buffer pointer to response to be dequeued.
 *
 * @return -1 when queue is empty, 0 on success.
 */
static inline int arp_dequeue_response(arp_queue_handle_t *queue, arp_request_t *response)
{
    if (arp_queue_empty_response(queue)) {
        return -1;
    }

    memcpy(response, &queue->response.queue[queue->response.head % queue->capacity], sizeof(arp_request_t));

    queue->response.queue[queue->response.head % queue->capacity].valid = false;
    queue->response.head++;

    return 0;
}

/**
 * Initialise the shared queue.
 *
 * @param queue queue handle to use.
 * @param capacity capacity of the free and active queues.
 */
static inline void arp_handle_init(arp_queue_handle_t *queue, uint32_t capacity)
{
    queue->capacity = capacity;
}
