#include "microkit.h"
#include <stdbool.h>
#include <stdint.h>
#include <os/sddf.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <sddf/network/queue.h>
#include <sddf/network/config.h>
#include <lions/firewall/checksum.h>
#include <lions/firewall/config.h>
#include <lions/firewall/common.h>
#include <lions/firewall/filter.h>
#include <lions/firewall/ip.h>
#include <lions/firewall/udp.h>
#include <lions/firewall/queue.h>

__attribute__((__section__(".fw_nat_config"))) fw_nat_config_t nat_config;

/* Incoming packets from filter */
fw_queue_t filter_queue;

/* Outgoing packets to router */
fw_queue_t router_queue;

/* Virtual address of rx buffer data region */
uintptr_t data_vaddr;

uint16_t snat_port = 49152;

static void translate(void)
{
    net_buff_desc_t buffer;

    while (!fw_queue_empty(&filter_queue)) {
        /* Incoming packet from filter */
        int err = fw_dequeue(&filter_queue, &buffer);
        assert(!err);

        uintptr_t pkt_vaddr = data_vaddr + buffer.io_or_offset;
        ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt_vaddr + IPV4_HDR_OFFSET);
        udp_hdr_t *udp_hdr = (udp_hdr_t *)(pkt_vaddr + transport_layer_offset(ip_hdr));


        if (FW_DEBUG_OUTPUT) {
            sddf_dprintf("%sUDP NAT LOG: src = %s:%u\n", fw_frmt_str[nat_config.interface], ipaddr_to_string(ip_hdr->src_ip,
                                                                                                             ip_addr_buf0), htons(udp_hdr->src_port));
            sddf_dprintf("%sUDP NAT LOG: dst = %s:%u\n", fw_frmt_str[nat_config.interface], ipaddr_to_string(ip_hdr->dst_ip,
                                                                                                             ip_addr_buf0), htons(udp_hdr->dst_port));
        }
        if (nat_config.snat) {
            if (FW_DEBUG_OUTPUT) {
                sddf_dprintf("%sUDP NAT LOG: to translate to %s:%u\n",
                             fw_frmt_str[nat_config.interface],
                             ipaddr_to_string(nat_config.snat, ip_addr_buf1),
                             snat_port);
            }
            snat_port++;
        } else {
            if (FW_DEBUG_OUTPUT) {
                sddf_dprintf("%sUDP NAT LOG: NAT disabled on this interface\n", fw_frmt_str[nat_config.interface]);
            }
        }

        if (FW_DEBUG_OUTPUT) {
            sddf_dprintf("%sUDP NAT LOG: src = %s:%u\n", fw_frmt_str[nat_config.interface], ipaddr_to_string(ip_hdr->src_ip,
                                                                                                             ip_addr_buf0), htons(udp_hdr->src_port));
            sddf_dprintf("%sUDP NAT LOG: dst = %s:%u\n", fw_frmt_str[nat_config.interface], ipaddr_to_string(ip_hdr->dst_ip,
                                                                                                             ip_addr_buf0), htons(udp_hdr->dst_port));
        }

        /* Send packet out to router */
        fw_enqueue(&router_queue, &buffer);
        assert(!err);

        microkit_notify(nat_config.router.ch);
    }
}

void notified(microkit_channel ch)
{
    if (ch == nat_config.filter.ch) {
        translate();
    } else {
        sddf_dprintf("%sUDP NAT LOG: Received notification on unknown channel: %d!\n", fw_frmt_str[nat_config.interface],
                     ch);
    }
}

void init(void)
{
    data_vaddr = (uintptr_t)nat_config.data.region.vaddr;

    fw_queue_init(&router_queue, nat_config.router.queue.vaddr,
                  sizeof(net_buff_desc_t),  nat_config.router.capacity);

    fw_queue_init(&filter_queue, nat_config.filter.queue.vaddr,
                  sizeof(net_buff_desc_t),  nat_config.filter.capacity);
}
