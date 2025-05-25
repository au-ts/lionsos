#pragma once

#include <stdint.h>
#include <string.h>
#include <os/sddf.h>
#include <sddf/util/util.h>
#include <sddf/timer/client.h>
#include <lions/firewall/config.h>
#include <lions/firewall/protocols.h>

typedef enum {
    ARP_ERR_OKAY = 0,   /* No error */
	ARP_ERR_FULL  /* Data structure is full */
} fw_arp_error_t;

typedef enum {
    ARP_STATE_INVALID,                  /* Whether this entry is valid entry in the table */
    ARP_STATE_PENDING,                  /* Whether this entry is still pending a response */
    ARP_STATE_UNREACHABLE,              /* Whether this ip is reachable and listed mac has meaning */
    ARP_STATE_REACHABLE
} fw_arp_entry_state_t;

typedef struct fw_arp_entry {
    fw_arp_entry_state_t state;                    /* State of this entry */
    uint32_t ip;                                /* IP of entry */
    uint8_t mac_addr[ETH_HWADDR_LEN];           /* MAC address of IP */
    uint8_t client;                             /* Bitmap of clients that initiated the request */
    uint8_t num_retries;                        /* Number of times we have sent out an arp request */
    uint64_t timestamp;                         /* Time of insertion */
} fw_arp_entry_t;

typedef struct fw_arp_table {
    fw_arp_entry_t *entries;
    uint16_t capacity;
} fw_arp_table_t;

typedef struct fw_arp_request {
    uint32_t ip;                        /* Requested IP */
    uint8_t mac_addr[ETH_HWADDR_LEN];   /* Zero filled or MAC of IP */
    fw_arp_entry_state_t state;            /* State of this ARP entry */
} fw_arp_request_t;

typedef struct fw_arp_queue {
    /* index to insert at */
    uint16_t tail;
    /* index to remove from */
    uint16_t head;
   /* arp array */
    fw_arp_request_t queue[FW_MAX_ARP_QUEUE_CAPACITY];
} fw_arp_queue_t;

typedef struct fw_arp_queue_handle {
    /* arp requests */
    fw_arp_queue_t request;
    /* responses to arp requests */
    fw_arp_queue_t response;
    /* capacity of the queues */
    uint32_t capacity;
} fw_arp_queue_handle_t;


/* Initialise the arp table data structure */
static void fw_arp_table_init(fw_arp_table_t *table,
    void *entries, 
    uint16_t capacity)
{
    table->entries = (fw_arp_entry_t *)entries;
    table->capacity = capacity;
}

/* Find an arp entry for an IP */
static fw_arp_entry_t *fw_arp_table_find_entry(fw_arp_table_t *table, uint32_t ip)
{
    for (uint16_t i = 0; i < table->capacity; i++) {
        fw_arp_entry_t *entry = table->entries + i;
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
static fw_arp_request_t fw_arp_response_from_entry(fw_arp_entry_t *entry) {
    fw_arp_request_t response = {entry->ip, {0}, entry->state};
    if (entry->state == ARP_STATE_REACHABLE) {
        memcpy(&response.mac_addr, &entry->mac_addr, ETH_HWADDR_LEN);
    }
    return response;
}

/* Add an entry to the arp table*/
static fw_arp_error_t fw_arp_table_add_entry(fw_arp_table_t *table,
                                       uint8_t timer_ch,
                                       fw_arp_entry_state_t state,
                                       uint32_t ip,
                                       uint8_t *mac_addr,
                                       uint8_t client)
{
    fw_arp_entry_t *slot = NULL;
    for (uint16_t i = 0; i < table->capacity; i++) {
        fw_arp_entry_t *entry = table->entries + i;

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
static inline uint16_t fw_arp_queue_length(fw_arp_queue_t *queue)
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
static inline bool fw_arp_queue_empty_request(fw_arp_queue_handle_t *queue)
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
static inline bool fw_arp_queue_empty_response(fw_arp_queue_handle_t *queue)
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
static inline bool fw_arp_queue_full_request(fw_arp_queue_handle_t *queue)
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
static inline bool fw_arp_queue_full_response(fw_arp_queue_handle_t *queue)
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
static inline int fw_arp_enqueue_request(fw_arp_queue_handle_t *queue, fw_arp_request_t request)
{
    if (fw_arp_queue_full_request(queue)) {
        return -1;
    }

    memcpy(&queue->request.queue[queue->request.tail % queue->capacity], &request, sizeof(fw_arp_request_t));
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
static inline int fw_arp_enqueue_response(fw_arp_queue_handle_t *queue, fw_arp_request_t response)
{
    if (fw_arp_queue_full_response(queue)) {
        return -1;
    }

    memcpy(&queue->response.queue[queue->response.tail % queue->capacity], &response, sizeof(fw_arp_request_t));
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
static inline int fw_arp_dequeue_request(fw_arp_queue_handle_t *queue, fw_arp_request_t *request)
{
    if (fw_arp_queue_empty_request(queue)) {
        return -1;
    }

    memcpy(request, &queue->request.queue[queue->request.head % queue->capacity], sizeof(fw_arp_request_t));
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
static inline int fw_arp_dequeue_response(fw_arp_queue_handle_t *queue, fw_arp_request_t *response)
{
    if (fw_arp_queue_empty_response(queue)) {
        return -1;
    }

    memcpy(response, &queue->response.queue[queue->response.head % queue->capacity], sizeof(fw_arp_request_t));
    queue->response.head++;

    return 0;
}

/**
 * Initialise the shared queue.
 *
 * @param queue queue handle to use.
 * @param capacity capacity of the free and active queues.
 */
static inline void fw_arp_handle_init(fw_arp_queue_handle_t *queue, uint32_t capacity)
{
    queue->capacity = capacity;
}
