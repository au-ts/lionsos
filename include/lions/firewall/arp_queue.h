#pragma once

#include <stdint.h>
#include <string.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include <sddf/timer/client.h>
#include <lions/firewall/config.h>
#include <lions/firewall/protocols.h>

typedef enum {
    ARP_ERR_OKAY = 0,   /* No error */
	ARP_ERR_FULL  /* Data structure is full */
} arp_error_t;

typedef enum {
    ARP_STATE_INVALID,                  /* Whether this entry is valid entry in the table */
    ARP_STATE_PENDING,                  /* Whether this entry is still pending a response */
    ARP_STATE_UNREACHABLE,              /* Whether this ip is reachable and listed mac has meaning */
    ARP_STATE_REACHABLE
} arp_entry_state_t;

typedef struct arp_entry {
    arp_entry_state_t state;                    /* State of this entry */
    uint32_t ip;                                /* IP of entry */
    uint8_t mac_addr[ETH_HWADDR_LEN];           /* MAC address of IP */
    uint8_t client;                             /* Bitmap of clients that initiated the request */
    uint8_t num_retries;                        /* Number of times we have sent out an arp request */
    uint64_t timestamp;                         /* Time of insertion */
} arp_entry_t;

typedef struct arp_table {
    arp_entry_t *entries;
    uint16_t capacity;
} arp_table_t;

typedef struct arp_request {
    uint32_t ip;                        /* Requested IP */
    uint8_t mac_addr[ETH_HWADDR_LEN];   /* Zero filled or MAC of IP */
    arp_entry_state_t state;            /* State of this ARP entry */
} arp_request_t;

typedef struct arp_queue {
    /* index to insert at */
    uint16_t tail;
    /* index to remove from */
    uint16_t head;
   /* arp array */
    arp_request_t queue[FIREWALL_MAX_ARP_QUEUE_CAPACITY];
} arp_queue_t;

typedef struct arp_queue_handle {
    /* arp requests */
    arp_queue_t request;
    /* responses to arp requests */
    arp_queue_t response;
    /* capacity of the queues */
    uint32_t capacity;
} arp_queue_handle_t;


/* Initialise the arp table data structure */
static void arp_table_init(arp_table_t *table,
    void *entries, 
    uint16_t capacity)
{
    table->entries = (arp_entry_t *)entries;
    table->capacity = capacity;
}

/* Find an arp entry for an IP */
static arp_entry_t *arp_table_find_entry(arp_table_t *table, uint32_t ip)
{
    for (uint16_t i = 0; i < table->capacity; i++) {
        arp_entry_t *entry = table->entries + i;
        if (entry->state == ARP_STATE_INVALID) {
            continue;
        }

        if (entry->ip == ip) {
            return entry;
        }
    }

    return NULL;
}

/* Create an arp response from an arp entry */
static arp_request_t arp_response_from_entry(arp_entry_t *entry) {
    arp_request_t response = {entry->ip, {0}, entry->state};
    if (entry->state == ARP_STATE_REACHABLE) {
        memcpy(&response.mac_addr, &entry->mac_addr, ETH_HWADDR_LEN);
    }
    return response;
}

/* Add an entry to the arp table*/
static arp_error_t arp_table_add_entry(arp_table_t *table,
                                       microkit_channel timer_ch,
                                       arp_entry_state_t state,
                                       uint32_t ip,
                                       uint8_t *mac_addr,
                                       uint8_t client)
{
    arp_entry_t *slot = NULL;
    for (uint16_t i = 0; i < table->capacity; i++) {
        arp_entry_t *entry = table->entries + i;

        if (entry->state == ARP_STATE_INVALID) {
            if (slot == NULL) {
                slot = entry;
            }
            continue;
        }

        /* Check for existing entries for this ip - there should only be one */
        if (entry->ip == ip) {
            slot = entry;
            break;
        }
    }

    if (slot == NULL) {
        return ARP_ERR_FULL;
    }

    slot->state = state;
    slot->ip = ip;
    if (mac_addr != NULL) {
        memcpy(&slot->mac_addr, mac_addr, ETH_HWADDR_LEN);
    }
    slot->client = BIT(client);
    slot->num_retries = 0;
    slot->timestamp = sddf_timer_time_now(timer_ch);

    return ARP_ERR_OKAY;
}

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
 * @param request request to be enqueued.
 *
 * @return -1 when queue is full, 0 on success.
 */
static inline int arp_enqueue_request(arp_queue_handle_t *queue, arp_request_t request)
{
    if (arp_queue_full_request(queue)) {
        return -1;
    }

    memcpy(&queue->request.queue[queue->request.tail % queue->capacity], &request, sizeof(arp_request_t));
    queue->request.tail++;

    return 0;
}

/**
 * Enqueue an element into an response queue.
 *
 * @param queue queue to enqueue into.
 * @param response response to be enqueued.
 *
 * @return -1 when queue is full, 0 on success.
 */
static inline int arp_enqueue_response(arp_queue_handle_t *queue, arp_request_t response)
{
    if (arp_queue_full_response(queue)) {
        return -1;
    }

    memcpy(&queue->response.queue[queue->response.tail % queue->capacity], &response, sizeof(arp_request_t));
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
