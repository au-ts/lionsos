/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stdint.h>
#include <lions/firewall/queue.h>
#include <lions/firewall/routing.h>

void pkt_waiting_init(pkts_waiting_t *pkts_waiting, void *packets, int16_t capacity)
{
    pkts_waiting->packets = (pkt_waiting_node_t *)packets;
    pkts_waiting->capacity = capacity;
    for (uint16_t i = 0; i < pkts_waiting->capacity; i++) {
        pkt_waiting_node_t *node = pkts_waiting->packets + i;
        /* Free list only maintains next pointers */
        node->next = i + 1;
    }
}

bool pkt_waiting_full(pkts_waiting_t *pkts_waiting)
{
    return pkts_waiting->size == pkts_waiting->capacity;
}

pkt_waiting_node_t *pkt_waiting_find_node(pkts_waiting_t *pkts_waiting, uint32_t ip)
{
    pkt_waiting_node_t *node = pkts_waiting->packets + pkts_waiting->head;
    for (uint16_t i = 0; i < pkts_waiting->length; i++) {
        if (node->ip == ip) {
            return node;
        }
        node = pkts_waiting->packets + node->next;
    }

    return NULL;
}

pkt_waiting_node_t *pkts_waiting_next_child(pkts_waiting_t *pkts_waiting, pkt_waiting_node_t *node)
{
    return pkts_waiting->packets + node->child;
}

fw_routing_err_t pkt_waiting_push_child(pkts_waiting_t *pkts_waiting, pkt_waiting_node_t *root, fw_buff_desc_t buffer)
{
    if (pkt_waiting_full(pkts_waiting)) {
        return ROUTING_ERR_FULL;
    }

    uint16_t new_idx = pkts_waiting->free;
    pkt_waiting_node_t *new_node = pkts_waiting->packets + new_idx;

    /* Update values */
    new_node->buffer = buffer;

    /* Update pointers */
    pkts_waiting->free = new_node->next;
    pkt_waiting_node_t *last_child = root;
    for (uint16_t i = 0; i < root->num_children; i++) {
        last_child = pkts_waiting_next_child(pkts_waiting, last_child);
    }
    last_child->child = new_idx;

    /* Update counts */
    root->num_children++;
    pkts_waiting->size++;

    return ROUTING_ERR_OKAY;
}

fw_routing_err_t pkt_waiting_push(pkts_waiting_t *pkts_waiting, uint32_t ip, fw_buff_desc_t buffer)
{
    if (pkt_waiting_full(pkts_waiting)) {
        return ROUTING_ERR_FULL;
    }

    uint16_t new_idx = pkts_waiting->free;
    pkt_waiting_node_t *new_node = pkts_waiting->packets + new_idx;

    /* Update values */
    new_node->num_children = 0;
    new_node->ip = ip;
    new_node->buffer = buffer;

    /* Update pointers */
    pkts_waiting->free = new_node->next;
    /* If this is not the first node */
    if (pkts_waiting->length) {
        uint16_t head_idx = pkts_waiting->head;
        pkt_waiting_node_t *head_node = pkts_waiting->packets + head_idx;

        new_node->next = head_idx;
        head_node->prev = new_idx;
    } else {
        pkts_waiting->tail = new_idx;
    }
    pkts_waiting->head = new_idx;

    /* Update counts */
    pkts_waiting->length++;
    pkts_waiting->size++;

    return ROUTING_ERR_OKAY;
}

fw_routing_err_t pkts_waiting_free_parent(pkts_waiting_t *pkts_waiting, pkt_waiting_node_t *root)
{
    /* First free children */
    uint16_t child_idx = root->child;
    pkt_waiting_node_t *child_node = pkts_waiting_next_child(pkts_waiting, root);
    for (uint16_t i = 0; i < root->num_children; i++) {

        /* Add to free list */
        child_node->next = pkts_waiting->free;
        pkts_waiting->free = child_idx;
        pkts_waiting->size--;

        /* Possibly free next child */
        child_idx = child_node->child;
        child_node = pkts_waiting_next_child(pkts_waiting, child_node);
    }

    /* Now free parent */
    uint16_t root_idx = (uint16_t)(root - pkts_waiting->packets);
    if (root_idx == pkts_waiting->head) {
        /* Root node is head */
        pkts_waiting->head = root->next;
    } else {
        pkt_waiting_node_t *prev_node = pkts_waiting->packets + root->prev;
        prev_node->next = root->next;
    }

    if (root_idx == pkts_waiting->tail) {
        /* Root node is tail */
        pkts_waiting->tail = root->prev;
    } else {
        pkt_waiting_node_t *next_node = pkts_waiting->packets + root->next;
        next_node->prev = root->prev;
    }

    root->next = pkts_waiting->free;
    pkts_waiting->free = root_idx;
    pkts_waiting->length--;
    pkts_waiting->size--;

    return ROUTING_ERR_OKAY;
}
