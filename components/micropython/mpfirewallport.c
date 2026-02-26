/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sddf/network/lib_sddf_lwip.h>
#include <sddf/network/util.h>
#include <lions/firewall/arp.h>
#include <lions/firewall/common.h>
#include <lions/firewall/config.h>
#include <lions/firewall/ethernet.h>
#include <lions/firewall/queue.h>
#include <lwip/ip.h>
#include <lwip/pbuf.h>
#include <lwip/sys.h>

#include "mpfirewallport.h"

#define dlog(fmt, ...)                                                                                                 \
    do {                                                                                                               \
        printf("%s: %s:%d:%s: " fmt "\n", microkit_name, __FILE__, __LINE__, __func__, ##__VA_ARGS__);                 \
    } while (0);

fw_webserver_interface_state_t fw_interface_state[FW_MAX_INTERFACES];
fw_routing_table_t *fw_routing_table;

extern fw_queue_t rx_active;
extern fw_queue_t rx_free[FW_MAX_INTERFACES];
extern fw_queue_t arp_req_queue;
extern fw_queue_t arp_resp_queue;

typedef struct __attribute__((__packed__)) arp_frame {
    eth_hdr_t eth_hdr;
    arp_pkt_t arp_pkt;
} arp_frame_t;

arp_frame_t arp_response_pkt = { 0 };

static bool notify_rx[FW_MAX_INTERFACES];
static bool notify_arp;

/* Custom pbuf free function to free pbuf holding ARP packet */
static void interface_free_arp_buffer(struct pbuf *buf)
{
    SYS_ARCH_DECL_PROTECT(old_level);
    pbuf_custom_offset_t *pbuf = (pbuf_custom_offset_t *)buf;
    SYS_ARCH_PROTECT(old_level);
    sddf_lwip_pbuf_pool_free(pbuf);
    SYS_ARCH_UNPROTECT(old_level);
}

static void firewall_interface_free_buffer(struct pbuf *buf)
{
    SYS_ARCH_DECL_PROTECT(old_level);
    pbuf_custom_offset_t *pbuf = (pbuf_custom_offset_t *)buf;
    SYS_ARCH_PROTECT(old_level);
    fw_buff_desc_t buffer = { pbuf->offset, 0 };
    assert(pbuf->region_id <= fw_config.num_interfaces);
    fw_enqueue(&rx_free[pbuf->region_id], &buffer);
    notify_rx[pbuf->region_id] = true;
    sddf_lwip_pbuf_pool_free(pbuf);
    SYS_ARCH_UNPROTECT(old_level);
}

static void fill_arp(uint32_t ip, uint8_t mac[ETH_HWADDR_LEN])
{
    /* Fill ethernet header */
    memcpy(arp_response_pkt.eth_hdr.ethdst_addr, fw_config.interfaces[fw_config.tx_interface].mac_addr, ETH_HWADDR_LEN);
    memcpy(arp_response_pkt.eth_hdr.ethsrc_addr, mac, ETH_HWADDR_LEN);
    arp_response_pkt.eth_hdr.ethtype = HTONS(ETH_TYPE_ARP);
    /* Fill ARP Packet */
    arp_response_pkt.arp_pkt.hwtype = HTONS(ARP_HWTYPE_ETH);
    arp_response_pkt.arp_pkt.protocol = HTONS(ETH_TYPE_IP);
    arp_response_pkt.arp_pkt.hwlen = ETH_HWADDR_LEN;
    arp_response_pkt.arp_pkt.protolen = ARP_PROTO_LEN_IPV4;
    arp_response_pkt.arp_pkt.opcode = HTONS(ARP_ETH_OPCODE_REPLY);
    memcpy(arp_response_pkt.arp_pkt.hwsrc_addr, mac, ETH_HWADDR_LEN);
    arp_response_pkt.arp_pkt.ipsrc_addr = ip;
    memcpy(arp_response_pkt.arp_pkt.hwdst_addr, fw_config.interfaces[fw_config.tx_interface].mac_addr, ETH_HWADDR_LEN);
    arp_response_pkt.arp_pkt.ipdst_addr = fw_config.interfaces[fw_config.tx_interface].ip;
}

bool mpfirewall_intercept_arp(struct pbuf *p)
{
    /* Check if this is an ARP request before transmitting through NIC */
    eth_hdr_t *eth_hdr = (eth_hdr_t *)p->payload;
    arp_pkt_t *arp_pkt = (arp_pkt_t *)(p->payload + ARP_PKT_OFFSET);
    if (eth_hdr->ethtype != HTONS(ETH_TYPE_ARP) || arp_pkt->opcode != HTONS(ARP_ETH_OPCODE_REQUEST)) {
        return false;
    }

    /* ARP request will be discarded or handled through the ARP requester */
    return true;
}

/**
 * Only invoked if p holds an ARP packet (i.e. firewall_catch_arp(p) == true).
 * ARP packets are not transmitted directly, instead they are converted to ARP
 * requests and passed to the ARP requester to handle.
 */
net_sddf_err_t mpfirewall_handle_arp(struct pbuf *p)
{
    /**
     * Check if the destination ip is ours - if so, this packet is most likely
     * an ARP probe. We should discard.
     */
    arp_pkt_t *arp_pkt = (arp_pkt_t *)(p->payload + ARP_PKT_OFFSET);
    if (arp_pkt->ipdst_addr == fw_config.interfaces[fw_config.tx_interface].ip) {
        return SDDF_LWIP_ERR_OK;
    }

    fw_arp_request_t request = { arp_pkt->ipdst_addr, { 0 }, ARP_STATE_INVALID };
    int err = fw_enqueue(&arp_req_queue, &request);
    if (err) {
        dlog("Could not enqueue arp request, queue is full");
        return SDDF_LWIP_ERR_NO_BUF;
    }

    notify_arp = true;
    return SDDF_LWIP_ERR_OK;
}

void mpfirewall_process_arp(void)
{
    while (!fw_queue_empty(&arp_resp_queue)) {
        fw_arp_request_t response;
        int err = fw_dequeue(&arp_resp_queue, &response);
        assert(!err);

        if (response.state == ARP_STATE_REACHABLE) {
            fill_arp(response.ip, response.mac_addr);

            if (FW_DEBUG_OUTPUT) {
                dlog("Inputting ARP response for ip %s -> obtained MAC[0] = %x, MAC[5] = %x\n",
                     ipaddr_to_string(response.ip, ip_addr_buf0), response.mac_addr[0], response.mac_addr[5]);
            }

            /* Input packet into lwip stack */
            pbuf_custom_offset_t *pbuf = sddf_lwip_pbuf_pool_alloc();
            if (!pbuf) {
                return;
            }
            pbuf->custom.custom_free_function = interface_free_arp_buffer;

            struct pbuf *p = pbuf_alloced_custom(PBUF_RAW, ARP_PKT_LEN, PBUF_REF, &pbuf->custom, &arp_response_pkt,
                                                 ARP_PKT_LEN);

            net_sddf_err_t net_err = sddf_lwip_input_pbuf(p);
            if (net_err != SDDF_LWIP_ERR_OK) {
                dlog("Failed to input ARP pbuf, error code %d\n", net_err);
                pbuf_free(p);
            }
        }
    }
}

void mpfirewall_process_rx(void)
{
    while (!fw_queue_empty(&rx_active) && !sddf_lwip_pbuf_pool_empty()) {
        pbuf_custom_offset_t *pbuf = sddf_lwip_pbuf_pool_alloc();
        if (!pbuf) {
            return;
        }

        fw_buff_desc_t buffer;
        int err = fw_dequeue(&rx_active, &buffer);
        assert(!err);

        // TODO: Currently the webserver can only transmit out one interface. So
        // if traffic is received on a non transmission interface, it is
        // immediately returned
        if (buffer.interface != fw_config.tx_interface) {
            assert(buffer.interface <= fw_config.num_interfaces);
            fw_enqueue(&rx_free[buffer.interface], &buffer);
            notify_rx[buffer.interface] = true;
            sddf_lwip_pbuf_pool_free(pbuf);
            continue;
        }

        pbuf->offset = buffer.offset;
        pbuf->region_id = buffer.interface;
        pbuf->custom.custom_free_function = firewall_interface_free_buffer;

        struct pbuf *p = pbuf_alloced_custom(
            PBUF_RAW, buffer.len, PBUF_REF, &pbuf->custom,
            (void *)(buffer.offset + fw_config.interfaces[buffer.interface].data.region.vaddr), NET_BUFFER_SIZE);

        net_sddf_err_t net_err = sddf_lwip_input_pbuf(p);
        if (net_err != SDDF_LWIP_ERR_OK) {
            dlog("Failed to input firewall pbuf, error code %d\n", net_err);
            pbuf_free(p);
        }
    }
}

void mpfirewall_handle_notify(void)
{
    if (notify_arp) {
        notify_arp = false;
        if (!microkit_have_signal) {
            microkit_deferred_notify(fw_config.arp_queue.ch);
        } else if (microkit_signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + fw_config.arp_queue.ch) {
            microkit_notify(fw_config.arp_queue.ch);
        }
    }

    for (uint8_t i = 0; i < fw_config.num_interfaces; i++) {
        if (notify_rx[i]) {
            notify_rx[i] = false;
            if (!microkit_have_signal) {
                microkit_deferred_notify(fw_config.interfaces[i].rx_free.ch);
            } else if (microkit_signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + fw_config.interfaces[i].rx_free.ch) {
                microkit_notify(fw_config.interfaces[i].rx_free.ch);
            }
        }
    }
}

void init_firewall_webserver(void)
{
    fw_routing_table = fw_config.router.routing_table.vaddr;
    for (uint8_t i = 0; i < fw_config.num_interfaces; i++) {
        for (uint8_t j = 0; j < fw_config.interfaces[i].num_filters; j++) {
            fw_interface_state[i].filter_states[j].rule_table = fw_config.interfaces[i].filters[j].rules.vaddr;
        }
    }
}
