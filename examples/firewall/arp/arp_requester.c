/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stdint.h>
#include <os/sddf.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <sddf/network/queue.h>
#include <sddf/network/config.h>
#include <sddf/network/util.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <lions/firewall/common.h>
#include <lions/firewall/config.h>
#include <lions/firewall/protocols.h>
#include <lions/firewall/arp_queue.h>
#include <string.h>

__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;

__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;

__attribute__((__section__(".fw_arp_requester_config"))) fw_arp_requester_config_t arp_config;

net_queue_handle_t rx_queue;
net_queue_handle_t tx_queue;

serial_queue_handle_t serial_tx_queue_handle;

/* Queues hold ARP requests/responses for router and webserver */
fw_arp_queue_handle_t *arp_queues[FW_NUM_ARP_REQUESTER_CLIENTS];

/* ARP table caches ARP request responses */
fw_arp_table_t arp_table;

/* Keep track of whether the tx virt requires notification */
static bool transmitted;

/* Keep track of which clients require notification */
static bool notify_client[FW_NUM_ARP_REQUESTER_CLIENTS] = {false};

#define ARP_MAX_RETRIES 5               /* How many times the ARP requester will send out an ARP request. */
#define ARP_RETRY_TIMER_S 5             /* How often to retry an ARP request, in seconds. */
#define ARP_RETRY_TIMER_NS (ARP_RETRY_TIMER_S * NS_IN_S)
#define ARP_CACHE_LIFE_M 5              /* The lifetime of the ARP cache in minutes. After this time elapses, the cache is flushed. */
#define ARP_CACHE_LIFE_NS ((ARP_CACHE_LIFE_M * 60) * NS_IN_S)
#define ARP_TICKS_PER_FLUSH (ARP_CACHE_LIFE_NS / ARP_RETRY_TIMER_NS) /* Number of arp ticks per arp cache flushing. */

/* Time that we will flush the arp queue (to the closest arp retry timer tick). */
uint64_t ticks_to_flush = ARP_TICKS_PER_FLUSH;

static void generate_arp(net_buff_desc_t *buffer, uint32_t ip)
{
    arp_packet_t *pkt = (arp_packet_t *)(net_config.tx_data.vaddr + buffer->io_or_offset);

    /* Set the destination MAC address as the broadcast MAC address */
    memset(&pkt->ethdst_addr, 0xFF, ETH_HWADDR_LEN);
    memcpy(&pkt->ethsrc_addr, arp_config.mac_addr, ETH_HWADDR_LEN);
    memcpy(&pkt->hwsrc_addr, arp_config.mac_addr, ETH_HWADDR_LEN);

    pkt->type = HTONS(ETH_TYPE_ARP);
    pkt->hwtype = HTONS(ETH_HWTYPE);
    pkt->proto = HTONS(ETH_TYPE_IP);
    pkt->hwlen = ETH_HWADDR_LEN;
    pkt->protolen = IPV4_PROTO_LEN;
    pkt->opcode = HTONS(ETHARP_OPCODE_REQUEST);

    /* Memset the hardware src addr to 0 for ARP requests */
    memset(&pkt->hwdst_addr, 0, ETH_HWADDR_LEN);
    pkt->ipdst_addr = ip;
    pkt->ipsrc_addr = arp_config.ip;
    memset(&pkt->padding, 0, 10);

    buffer->len = 56;
}

static void process_requests()
{
    for (uint8_t client = 0; client < arp_config.num_arp_clients; client++) {
        while (!fw_arp_queue_empty_request(arp_queues[client]) && !net_queue_empty_free(&tx_queue)) {
            fw_arp_request_t request;
            int err = fw_arp_dequeue_request(arp_queues[client], &request);
            assert(!err);

            /* Check if an arp entry already exists */
            fw_arp_entry_t *entry = fw_arp_table_find_entry(&arp_table, request.ip);
            if (entry != NULL && entry->state != ARP_STATE_PENDING) {
                /* Reply immediately */
                fw_arp_enqueue_response(arp_queues[client], fw_arp_response_from_entry(entry));
                notify_client[client] = true;
                continue;
            } else if (entry != NULL && entry->state == ARP_STATE_PENDING) {
                /* Notify client upon response for existing ARP request */
                entry->client |= BIT(client);
                continue;
            }
            

            /* Generate ARP request */
            net_buff_desc_t buffer = {};
            err = net_dequeue_free(&tx_queue, &buffer);
            assert(!err);

            generate_arp(&buffer, request.ip);
            err = net_enqueue_active(&tx_queue, buffer);
            assert(!err);

            if (FW_DEBUG_OUTPUT) {
                sddf_printf("%sARP requester processing client %u request for ip %s\n",
                    fw_frmt_str[arp_config.interface], client,
                    ipaddr_to_string(request.ip, ip_addr_buf0));
            }

            /* Create arp entry for request to store associated client */
            fw_arp_error_t arp_err = fw_arp_table_add_entry(&arp_table, timer_config.driver_id, ARP_STATE_PENDING, request.ip, NULL, client);
            if (arp_err == ARP_ERR_FULL) {
                sddf_dprintf("%sARP REQUESTER LOG: Arp cache full, cannot enqueue entry!\n",
                fw_frmt_str[arp_config.interface]);
            }

            transmitted = true;
        }
    }
}

static void process_responses()
{
    bool returned = false;
    bool reprocess = true;
    while (reprocess) {
        while (!net_queue_empty_active(&rx_queue)) {
            net_buff_desc_t buffer;
            int err = net_dequeue_active(&rx_queue, &buffer);
            assert(!err);

            arp_packet_t *pkt = (arp_packet_t *)(net_config.rx_data.vaddr + buffer.io_or_offset);
            /* Check if packet is an ARP request */
            if (pkt->type == HTONS(ETH_TYPE_ARP)) {
                /* Check if it's a probe, ignore announcements */
                if (pkt->opcode == HTONS(ETHARP_OPCODE_REPLY)) {
                    /* Find the arp entry */
                    fw_arp_entry_t *entry = fw_arp_table_find_entry(&arp_table, pkt->ipsrc_addr);
                    if (entry != NULL) {
                        /* This was a response to a request we sent, update entry */
                        entry->state = ARP_STATE_REACHABLE;
                        memcpy(&entry->mac_addr, &pkt->hwsrc_addr, ETH_HWADDR_LEN);

                        /* Send to clients */
                        for (uint8_t client = 0; client < arp_config.num_arp_clients; client++) {
                            if (BIT(client) & entry->client) {
                                fw_arp_enqueue_response(arp_queues[client], fw_arp_response_from_entry(entry));
                                notify_client[client] = true;
                                if (FW_DEBUG_OUTPUT) {
                                    sddf_printf("%sARP requester received response for client %u, ip %s. MAC[0] = %x, MAC[5] = %x\n",
                                        fw_frmt_str[arp_config.interface], client, ipaddr_to_string(pkt->ipsrc_addr, ip_addr_buf0),
                                        pkt->hwsrc_addr[0], pkt->hwsrc_addr[5]);
                                }
                            }
                        }
                    } else {
                        /* Create a new entry */
                        fw_arp_error_t arp_err = fw_arp_table_add_entry(&arp_table, timer_config.driver_id, ARP_STATE_REACHABLE, pkt->ipsrc_addr, pkt->hwsrc_addr, 0);
                        if (arp_err == ARP_ERR_FULL) {
                            sddf_dprintf("%sARP REQUESTER LOG: Arp cache full, cannot enqueue entry!\n", fw_frmt_str[arp_config.interface]);
                        }
                    }
                }
            }

            buffer.len = 0;
            err = net_enqueue_free(&rx_queue, buffer);
            assert(!err);
            returned = true;
        }

        net_request_signal_active(&rx_queue);
        reprocess = false;

        if (!net_queue_empty_active(&rx_queue)) {
            net_cancel_signal_active(&rx_queue);
            reprocess = true;
        }
    }

    if (returned && net_require_signal_free(&rx_queue)) {
        net_cancel_signal_free(&rx_queue);
        microkit_deferred_notify(net_config.rx.id);
    }
}

/* Returns the number of ARP entry retries. */
static uint16_t process_retries(void)
{
    uint16_t pending_requests = 0;
    for (uint16_t i = 0; i < arp_table.capacity; i++) {
        fw_arp_entry_t *entry = arp_table.entries + i;
        if (entry->state != ARP_STATE_PENDING) {
            continue;
        }

        if (entry->num_retries >= ARP_MAX_RETRIES) {
            /* Node is now considered unreachable */
            entry->state = ARP_STATE_UNREACHABLE;

            /* Generate ARP responses */
            for (uint8_t client = 0; client < arp_config.num_arp_clients; client++) {
                if (BIT(client) & entry->client) {
                    fw_arp_enqueue_response(arp_queues[client], fw_arp_response_from_entry(entry));
                    notify_client[client] = true;
                }
            }
        } else {
            /* Resend the ARP request out to the network */

            if (FW_DEBUG_OUTPUT) {
                sddf_printf("%sARP requester attempting to resend request for ip %s\n",
                    fw_frmt_str[arp_config.interface],
                    ipaddr_to_string(entry->ip, ip_addr_buf0));
            }

            if (!net_queue_empty_free(&tx_queue)) {
                net_buff_desc_t buffer = {0};
                int err = net_dequeue_free(&tx_queue, &buffer);
                assert(!err);

                generate_arp(&buffer, entry->ip);
                err = net_enqueue_active(&tx_queue, buffer);
                assert(!err);
                transmitted = true;

                if (FW_DEBUG_OUTPUT) {
                    sddf_printf("%sARP requester resent request for ip %s\n",
                        fw_frmt_str[arp_config.interface],
                        ipaddr_to_string(entry->ip, ip_addr_buf0));
                }
            }

            /* Increment the number of retries */
            entry->num_retries++;
            pending_requests++;
        }
    }

    return pending_requests;
}

/* Flush all non pending cache entries */
static uint16_t arp_table_flush(void) {
    uint16_t flushed = 0;
    for (uint16_t i = 0; i < arp_table.capacity; i++) {
        fw_arp_entry_t *entry = arp_table.entries + i;
        if (entry->state == ARP_STATE_INVALID || entry->state == ARP_STATE_PENDING) {
            continue;
        }

        entry->state = ARP_STATE_INVALID;
        flushed++;
    }

    return flushed;
}

void init(void)
{
    assert(net_config_check_magic((void *)&net_config));

    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
        serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);

    net_queue_init(&rx_queue, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
        net_config.rx.num_buffers);
    net_queue_init(&tx_queue, net_config.tx.free_queue.vaddr, net_config.tx.active_queue.vaddr,
        net_config.tx.num_buffers);
    net_buffers_init(&tx_queue, 0);

    for (uint8_t client = 0; client < arp_config.num_arp_clients; client++) {
        arp_queues[client] = (fw_arp_queue_handle_t *) arp_config.arp_clients[client].queue.vaddr;
        fw_arp_handle_init(arp_queues[client], arp_config.arp_clients[client].capacity);
    }

    fw_arp_table_init(&arp_table, (fw_arp_entry_t *)arp_config.arp_cache.vaddr, arp_config.arp_cache_capacity);

    /* Set the first tick */
    sddf_timer_set_timeout(timer_config.driver_id, ARP_RETRY_TIMER_NS);
}

void notified(microkit_channel ch)
{
    if (ch == arp_config.arp_clients[0].ch || (arp_config.num_arp_clients == 2 && ch == arp_config.arp_clients[1].ch)) {
        process_requests();
    } if (ch == net_config.rx.id) {
        process_responses();
    } else if (ch == timer_config.driver_id) {
        ticks_to_flush--;
        if (ticks_to_flush != 0) {
            uint16_t retries = process_retries();

            if (FW_DEBUG_OUTPUT && retries > 0) {
                sddf_printf("%sARP requester processed %u retries for tick %lu\n",
                    fw_frmt_str[arp_config.interface], retries, ticks_to_flush);
            }

        } else {
            uint16_t flushed = arp_table_flush();

            if (FW_DEBUG_OUTPUT && flushed > 0) {
                sddf_printf("%sARP requester flushed %u entries from cache\n",
                    fw_frmt_str[arp_config.interface], flushed);
            }

            ticks_to_flush = ARP_TICKS_PER_FLUSH;
        }

        sddf_timer_set_timeout(timer_config.driver_id, ARP_RETRY_TIMER_NS);
    }

    if (transmitted && net_require_signal_active(&tx_queue)) {
        transmitted = false;
        net_cancel_signal_active(&tx_queue);
        microkit_deferred_notify(net_config.tx.id);
    }

    for (uint8_t client = 0; client < arp_config.num_arp_clients; client++) {
        if (notify_client[client]) {
            notify_client[client] = false;
            microkit_notify(arp_config.arp_clients[client].ch);
        }
    }
}
