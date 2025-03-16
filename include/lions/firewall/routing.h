#pragma once

#include <stdint.h>
#include <lions/firewall/linkedlist.h>
#include <lions/firewall/queue.h>

#define NUM_ROUTES 10

typedef struct routing_entry {
    uint32_t network_id;
    uint32_t subnet_mask;
    uint32_t next_hop;
    /* kwinter: Not sure if out_interface is useful in our system. */
    // uint8_t out_interface;
    /* @kwinter: this metric field supposedly keeps a value for the min number
       hops to reach network_id. */
    // uint16_t metric;
} routing_entry_t;

/* Queue entry for packets awaiting arp requests before transmission */
typedef struct llnode_pkt_waiting {
    /* First two nodes of the ll implementation must be next and prev.s */
    void *next;
    void *prev;

    void *next_ip_match;
    uint32_t ip;
    // TODO: Implement validity of waiting packets
    bool valid;
    firewall_buff_desc_t buffer;
} llnode_pkt_waiting_t;
