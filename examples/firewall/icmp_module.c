/*
* Copyright 2025, UNSW
* SPDX-License-Identifier: BSD-2-Clause
*/

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <sddf/network/queue.h>
#include <sddf/network/config.h>
#include <sddf/network/util.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <lions/firewall/config.h>
#include <lions/firewall/protocols.h>
#include <lions/firewall/icmp_queue.h>


__attribute__((__section__(".fw_icmp_module_config"))) fw_icmp_module_config_t icmp_config;

__attribute__((__section__(".net1_client_config"))) net_client_config_t net1_config;

__attribute__((__section__(".net2_client_config"))) net_client_config_t net2_config;

typedef struct state {
    net_queue_handle_t net1_queue;
    net_queue_handle_t net2_queue;
    icmp_queue_handle_t icmp_queue;
} state_t;

state_t state;

/* This code is taken from: https://gist.github.com/david-hoze/0c7021434796997a4ca42d7731a7073a */
uint16_t checksum(uint8_t *buf, uint16_t len)
{
    uint32_t sum = 0;

    while(len >1){
        sum += 0xFFFF & (*buf<<8|*(buf+1));
        buf+=2;
        len-=2;
    }

    // if there is a byte left then add it (padded with zero)
    if (len){
        sum += 0xFFFF & (*buf<<8|0x00);
    }

    // now calculate the sum over the bytes in the sum
    // until the result is only 16bit long
    while (sum>>16){
            sum = (sum & 0xFFFF)+(sum >> 16);
    }
    // build 1's complement:
    return( (uint16_t) sum ^ 0xFFFF);
}

void generate_icmp(int out_net)
{
    bool transmitted = false;
    net_queue_handle_t *curr_net;
    if (out_net == 1) {
        curr_net = &state.net1_queue;
    } else {
        curr_net = &state.net2_queue;
    }
    while (!icmp_queue_empty(&state.icmp_queue) && !net_queue_empty_free(curr_net)) {
        icmp_req_t req = {0};
        int err = icmp_dequeue(&state.icmp_queue, &req);
        assert(!err);

        net_buff_desc_t buffer = {};
        err = net_dequeue_free(curr_net, &buffer);
        assert(!err);

        icmphdr_t *icmp_resp = (icmphdr_t *) (net1_config.tx_data.vaddr + buffer.io_or_offset);
        // Construct the IP header for the response.
        sddf_memcpy(&icmp_resp->ip_hdr.ethdst_addr, &req.old_hdr.ethsrc_addr, ETH_HWADDR_LEN);
        sddf_memcpy(&icmp_resp->ip_hdr.ethdst_addr, &net1_config.mac_addr, ETH_HWADDR_LEN);
        icmp_resp->ip_hdr.type = HTONS(ETH_TYPE_IP);
        icmp_resp->ip_hdr.version = 4;
        icmp_resp->ip_hdr.ihl = 20;
        // The differentiated services code 48 is for network control traffic.
        icmp_resp->ip_hdr.tos = 48;
        // Hardcode the total length of a destination unreachable packet here.
        // Will contain two ip headers (ours and the old one), as well as 32 bits for icmp header
        // and 64 bits for the old data.
        icmp_resp->ip_hdr.tot_len = (sizeof(ipv4_packet_t) * 2) + 8 + 4;
        // Not fragmenting this IP packet.
        icmp_resp->ip_hdr.id = 0;
        icmp_resp->ip_hdr.frag_off = 0;
        // Recommended inital value of ttl is 64 hops according to the TCP/IP spec.
        // @kwinter: Alot of places use 255 TTL as the default?
        icmp_resp->ip_hdr.ttl = 64;
        icmp_resp->ip_hdr.protocol = HTONS(IPV4_PROTO_ICMP);
        // @kwinter: Set ip header checksum to 0 and let hardware fill this in.
        // TODO: Hide this behind an ifdef if we have hardware checksumming support.
        icmp_resp->ip_hdr.check = 0;
        // @kwinter: Is the src ip in this cases just wherever the original packet
        // was addressed to, or the IP of our NIC.
        icmp_resp->ip_hdr.src_ip = req.old_hdr.dst_ip;
        icmp_resp->ip_hdr.dst_ip = req.old_hdr.src_ip;
        icmp_resp->type = HTONS(req.type);
        // @kwinter: Not sure what subtype of dest unreachable we want to use.
        icmp_resp->code = HTONS(req.code);
        // Checksum needs to be 0 for currect checksum calculation.
        icmp_resp->checksum = 0;
        sddf_memcpy(&icmp_resp->un.dest_unreach.old_ip_hdr, &req.old_hdr, sizeof(ipv4_packet_t));
        icmp_resp->un.dest_unreach.old_data = req.old_data;
        // Now that the packet is constructed, calculate the final checksum.
        icmp_resp->checksum = checksum((char *) (icmp_resp + sizeof(ipv4_packet_t)), sizeof(icmp_resp) - sizeof(ipv4_packet_t));
        buffer.len = icmp_resp->ip_hdr.tot_len;
        err = net_enqueue_active(curr_net, buffer);

        assert(!err);
    }
}

void init(void)
{
    /* Setup the queue with the router. */
    icmp_queue_init(&state.icmp_queue, icmp_config.router1_conn.queue.vaddr, icmp_config.router1_conn.capacity);
    icmp_queue_init(&state.icmp_queue, icmp_config.router2_conn.queue.vaddr, icmp_config.router2_conn.capacity);

    /* Setup the queue with the transmit virtualisers. */
    net_queue_init(&state.net1_queue, net1_config.tx.free_queue.vaddr, net1_config.tx.active_queue.vaddr,
        net1_config.tx.num_buffers);
    net_buffers_init(&state.net1_queue, 0);

    net_queue_init(&state.net2_queue, net2_config.tx.free_queue.vaddr, net2_config.tx.active_queue.vaddr,
        net2_config.tx.num_buffers);
    net_buffers_init(&state.net2_queue, 0);
}

void notified(microkit_channel ch)
{
    if (ch == icmp_config.router1_conn.ch) {
        sddf_dprintf("Notified by router 1 to send out icmp\n");
        // Set out net argument to network 1 - this ICMP packet will
        // go out to the external network.
        generate_icmp(1);
    } else if (ch == icmp_config.router2_conn.ch) {
        sddf_dprintf("Notified by router 2 to send out icmp\n");
        // Set out net argument to network 2 - this ICMP packet will
        // go out to the internal network.
        generate_icmp(2);
    }
}