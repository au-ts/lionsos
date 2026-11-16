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
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <lions/firewall/checksum.h>
#include <lions/firewall/config.h>
#include <lions/firewall/common.h>
#include <lions/firewall/filter.h>
#include <lions/firewall/ip.h>
#include <lions/firewall/tcp.h>
#include <lions/firewall/queue.h>
#include <lions/firewall/tcp_filter.h>

__attribute__((__section__(".fw_filter_config"))) fw_filter_config_t filter_config;
__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;

/* Queues for receiving and transmitting packets */
net_queue_handle_t rx_queue;
net_queue_handle_t tx_queue;
fw_queue_t router_queue;

/* Holds filtering rules and state */
fw_filter_state_t filter_state;

/* Interval between ticks, upon which timed out instances are removed */
#define TCP_TICK_NS (10 * NS_IN_S)

/* Time without traffic before an instance is removed, for each connection state */
#define TCP_FIRST_TIMEOUT_NS (120 * NS_IN_S)            /* syn sent, no reply yet */
#define TCP_OPENING_TIMEOUT_NS (30 * NS_IN_S)           /* syn-ack sent */
#define TCP_ESTABLISHED_TIMEOUT_NS (24 * 60 * 60 * NS_IN_S)
#define TCP_CLOSING_TIMEOUT_NS (15 * 60 * NS_IN_S)      /* one side has sent a fin */
#define TCP_FIN_WAIT_TIMEOUT_NS (45 * NS_IN_S)          /* both sides have sent a fin */
#define TCP_CLOSED_TIMEOUT_NS (90 * NS_IN_S)            /* closed or reset */

/* Timeouts are reduced once the instance table is 60% full, down to a third of
normal when full */
#define TCP_ADAPTIVE_START_PERCENT 60
#define TCP_ADAPTIVE_END_PERCENT 120

/* Shortest timeout, longer than the neighbour filter's time can lag behind */
#define TCP_MIN_TIMEOUT_NS (3 * TCP_TICK_NS)

/* Current time in ns, updated each tick */
uint64_t now_ns = 0;

static uint64_t state_timeout(fw_tcp_conn_state_t conn_state)
{
    switch (conn_state) {
    case TCP_SYN_SENT:
    case TCP_SYN_SEEN:
    case TCP_SYN_BOTH:
        return TCP_FIRST_TIMEOUT_NS;
    case TCP_SYNACK_SENT:
    case TCP_SYNACK_SEEN:
        return TCP_OPENING_TIMEOUT_NS;
    case TCP_ESTABLISHED:
        return TCP_ESTABLISHED_TIMEOUT_NS;
    case TCP_FIN_SENT:
    case TCP_FIN_SEEN:
        return TCP_CLOSING_TIMEOUT_NS;
    case TCP_FINACK_SENT:
    case TCP_FINACK_SEEN:
        return TCP_FIN_WAIT_TIMEOUT_NS;
    default:
        return TCP_CLOSED_TIMEOUT_NS;
    }
}

static void log_instance(const char *msg, fw_tcp_instance_t *instance)
{
    LOG_FIREWALL("TCP FILTER", "on interface %u %s for rule %u: (ip %s, port %u) -> (ip %s, port %u)\n",
                 filter_config.interface, msg, instance->rule_id, ipaddr_to_string(instance->src_ip, ip_addr_buf0),
                 htons(instance->src_port), ipaddr_to_string(instance->dst_ip, ip_addr_buf1),
                 htons(instance->dst_port));
}

/* Scale a timeout down as the instance table fills */
static uint64_t adaptive_timeout(uint64_t timeout_ns, uint16_t num_instances)
{
    uint32_t start = (uint32_t)filter_state.instances_capacity * TCP_ADAPTIVE_START_PERCENT / 100;
    uint32_t end = (uint32_t)filter_state.instances_capacity * TCP_ADAPTIVE_END_PERCENT / 100;
    if (num_instances <= start) {
        return timeout_ns;
    }

    return timeout_ns / (end - start) * (end - num_instances);
}

/* Remove instances created by this filter which have not seen traffic within
the timeout of their connection state */
static void remove_timed_out_instances(void)
{
    uint16_t num_instances = filter_state.internal_instances_table->size;
    fw_tcp_instance_t *slots = fw_tcp_slots(filter_state.internal_instances_table);
    for (uint16_t i = 0; i < filter_state.instances_capacity; i++) {
        fw_tcp_instance_t *instance = slots + i;
        if (instance->slot_state != FW_TCP_SLOT_VALID) {
            continue;
        }

        fw_tcp_packet_data_t *internal_tracking = NULL;
        fw_tcp_packet_data_t *external_tracking = NULL;
        fw_tcp_conn_state_t conn_state = TCP_NONE;
        fw_filter_err_t fw_err = fw_tcp_extract_state(instance, true, &internal_tracking, &external_tracking,
                                                      &conn_state);
        uint64_t timeout_ns = (fw_err == FILTER_ERR_OKAY) ? state_timeout(conn_state) : TCP_CLOSED_TIMEOUT_NS;
        timeout_ns = adaptive_timeout(timeout_ns, num_instances);
        if (timeout_ns < TCP_MIN_TIMEOUT_NS) {
            timeout_ns = TCP_MIN_TIMEOUT_NS;
        }

        uint64_t timestamp = __atomic_load_n(&instance->timestamp, __ATOMIC_RELAXED);
        if (fw_tcp_elapsed(now_ns, timestamp) < timeout_ns) {
            continue;
        }

        if (fw_tcp_filter_expire_instance(&filter_state, instance, timestamp, now_ns)) {
            log_instance("removing timed out connection", instance);
        }
    }

    fw_tcp_filter_clear_deleted(&filter_state, now_ns);
}

/* Remove instances created by a connect rule which has been removed */
static fw_filter_err_t remove_rule_instances(fw_filter_state_t *state, uint16_t rule_id)
{
    fw_tcp_instance_t *slots = fw_tcp_slots(state->internal_instances_table);
    for (uint16_t i = 0; i < state->instances_capacity; i++) {
        if (slots[i].slot_state == FW_TCP_SLOT_VALID && slots[i].rule_id == rule_id) {
            fw_tcp_filter_remove_instance(state, slots + i, now_ns);
        }
    }

    return FILTER_ERR_OKAY;
}

/* Check a received packet isn't a fragment, as fragments are not reassembled, and
is long enough for its IP and TCP headers */
static bool packet_valid(net_buff_desc_t *buffer, ipv4_hdr_t *ip_hdr)
{
    if (buffer->len < IPV4_HDR_OFFSET + IPV4_HDR_LEN_MIN) {
        return false;
    }

    if (ip_hdr->more_frag || ip_hdr->frag_offset1 || ip_hdr->frag_offset2) {
        return false;
    }

    uint16_t total_len = htons(ip_hdr->tot_len);
    if (ipv4_header_length(ip_hdr) < IPV4_HDR_LEN_MIN || total_len < ipv4_header_length(ip_hdr) + sizeof(tcp_hdr_t)) {
        return false;
    }

    return buffer->len >= IPV4_HDR_OFFSET + total_len;
}

/* Extract the details of a TCP packet needed for tracking. Returns false if the
header lengths are invalid. */
static bool parse_packet(ipv4_hdr_t *ip_hdr, tcp_hdr_t *tcp_hdr, fw_tcp_packet_t *pkt)
{
    uint16_t tcp_hdr_len = 4 * tcp_hdr->doff;
    uint16_t headers_len = ipv4_header_length(ip_hdr) + tcp_hdr_len;
    uint16_t total_len = htons(ip_hdr->tot_len);
    if (tcp_hdr_len < sizeof(tcp_hdr_t) || headers_len > total_len) {
        return false;
    }
    uint32_t payload_len = total_len - headers_len;

    pkt->syn = tcp_hdr->syn;
    pkt->ack = tcp_hdr->ack;
    pkt->fin = tcp_hdr->fin;
    pkt->rst = tcp_hdr->rst;
    pkt->seq = htonl(tcp_hdr->seq);
    pkt->ack_seq = pkt->ack ? htonl(tcp_hdr->ack_seq) : 0;
    pkt->fin_seq = pkt->seq + payload_len;
    pkt->end = pkt->seq + payload_len + pkt->syn + pkt->fin;
    pkt->window = htons(tcp_hdr->window);
    pkt->wscale = FW_TCP_NO_WSCALE;

    if (!pkt->syn) {
        return true;
    }

    /* Find the window scale option */
    uint8_t *opt = (uint8_t *)tcp_hdr + sizeof(tcp_hdr_t);
    uint8_t *opt_end = (uint8_t *)tcp_hdr + tcp_hdr_len;
    while (opt < opt_end) {
        uint8_t kind = opt[0];
        if (kind == 0) {
            break;
        }
        if (kind == 1) {
            opt++;
            continue;
        }
        if (opt + 1 >= opt_end || opt[1] < 2 || opt + opt[1] > opt_end) {
            break;
        }
        if (kind == 3 && opt[1] == 3) {
            pkt->wscale = opt[2] > FW_TCP_MAX_WSCALE ? FW_TCP_MAX_WSCALE : opt[2];
        }
        opt += opt[1];
    }

    return true;
}

/* The neighbour filter claims an instance when it first accepts return traffic,
so return traffic on any other interface is dropped */
static bool claim_if_neighbour(fw_tcp_instance_t *instance, bool is_internal)
{
    return is_internal || fw_tcp_filter_claim_instance(instance, filter_config.interface);
}

/**
 * Track a packet matching a connect rule or an existing instance.
 *
 * @return whether the packet should be forwarded.
 */
static bool track_packet(ipv4_hdr_t *ip_hdr, tcp_hdr_t *tcp_hdr, uint16_t rule_id, fw_tcp_instance_t *instance,
                         bool is_internal)
{
    fw_tcp_packet_t pkt;
    if (!parse_packet(ip_hdr, tcp_hdr, &pkt)) {
        LOG_FIREWALL("TCP FILTER", "on interface %u dropping packet with invalid header length\n",
                     filter_config.interface);
        return false;
    }

    fw_tcp_packet_data_t *internal_tracking = NULL;
    fw_tcp_packet_data_t *external_tracking = NULL;
    fw_tcp_conn_state_t conn_state = TCP_NONE;
    fw_filter_err_t fw_err = fw_tcp_extract_state(instance, is_internal, &internal_tracking, &external_tracking,
                                                  &conn_state);
    if (fw_err != FILTER_ERR_OKAY) {
        LOG_FIREWALL_ERR("TCP FILTER", "on interface %u could not extract connection state: %s\n",
                         filter_config.interface, fw_filter_err_str[fw_err]);
        return false;
    }

    /* A valid rst ends the connection immediately */
    if (pkt.rst) {
        if (!fw_tcp_rst_valid(conn_state, internal_tracking, external_tracking, &pkt)
            || !claim_if_neighbour(instance, is_internal)) {
            return false;
        }
        __atomic_fetch_or(&internal_tracking->flags, FW_TCP_RST_BIT, __ATOMIC_RELEASE);
        __atomic_store_n(&instance->timestamp, now_ns, __ATOMIC_RELAXED);
        return true;
    }

    /* Retransmitted syns and syn-acks, until this side sends anything else */
    if (instance != NULL && pkt.syn && !pkt.fin && pkt.end == internal_tracking->end
        && ((!pkt.ack && internal_tracking->flags == FW_TCP_SYN_BIT)
            || (pkt.ack && internal_tracking->flags == (FW_TCP_SYN_BIT | FW_TCP_ACK_BIT)
                && pkt.ack_seq == internal_tracking->ack))) {
        if (!claim_if_neighbour(instance, is_internal)) {
            return false;
        }
        __atomic_store_n(&instance->timestamp, now_ns, __ATOMIC_RELAXED);
        return true;
    }

    /* A new connection from the same side can reuse a finished one's ports */
    bool new_syn = pkt.syn && !pkt.ack && !pkt.fin;
    if ((conn_state == TCP_CLOSED || conn_state == TCP_RESET) && new_syn && is_internal) {
        log_instance("replacing finished connection", instance);
        fw_tcp_filter_remove_instance(&filter_state, instance, now_ns);
        instance = NULL;
        conn_state = TCP_NONE;
    }

    bool permitted = false;
    switch (conn_state) {
    case TCP_NONE:
        /* Only a syn may start a connection */
        if (!new_syn) {
            return false;
        }

        fw_err = fw_tcp_filter_add_instance(&filter_state, ip_hdr->src_ip, tcp_hdr->src_port, ip_hdr->dst_ip,
                                            tcp_hdr->dst_port, rule_id, now_ns, &instance);
        if (fw_err != FILTER_ERR_OKAY) {
            LOG_FIREWALL_ERR("TCP FILTER", "on interface %u could not establish connection for rule %u: (ip %s, "
                             "port %u) -> (ip %s, port %u): %s\n", filter_config.interface, rule_id,
                             ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0), htons(tcp_hdr->src_port),
                             ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1), htons(tcp_hdr->dst_port),
                             fw_filter_err_str[fw_err]);
            return false;
        }

        fw_tcp_record_packet(&instance->internal, &instance->external, &pkt);
        fw_tcp_filter_publish_instance(&filter_state, instance);
        log_instance("establishing connection", instance);
        return true;
    case TCP_SYN_SEEN:
        /* A syn-ack, or a syn crossing it (simultaneous open) */
        permitted = (pkt.syn && pkt.ack && !pkt.fin && pkt.ack_seq == external_tracking->end) || new_syn;
        break;
    case TCP_SYN_BOTH:
        /* Simultaneous open, only a syn-ack */
        permitted = pkt.syn && pkt.ack && !pkt.fin && pkt.end == internal_tracking->end
                 && pkt.ack_seq == external_tracking->end;
        break;
    case TCP_SYNACK_SEEN:
        /* The final handshake ack, or a syn-ack in a simultaneous open */
        permitted = pkt.ack && pkt.ack_seq == external_tracking->end
                 && ((!pkt.syn && pkt.seq == internal_tracking->end)
                     || (pkt.syn && !pkt.fin && pkt.end == internal_tracking->end));
        break;
    case TCP_ESTABLISHED:
        /* No syns, and data must lie within the windows */
        permitted = !pkt.syn && pkt.ack && fw_tcp_in_window(internal_tracking, external_tracking, &pkt);
        break;
    case TCP_FIN_SENT:
        /* This side has closed: retransmissions and acks, but no new data */
        permitted = fw_tcp_after_fin_valid(internal_tracking, external_tracking, &pkt);
        break;
    case TCP_FIN_SEEN:
        /* Other side has closed: this side may still send data and its own fin */
        permitted = !pkt.syn && pkt.ack && fw_tcp_in_window(internal_tracking, external_tracking, &pkt);
        break;
    case TCP_FINACK_SENT:
    case TCP_FINACK_SEEN:
    case TCP_CLOSED:
        /* Both sides have sent a fin, only retransmissions and acks */
        permitted = fw_tcp_after_fin_valid(internal_tracking, external_tracking, &pkt);
        break;
    default:
        /* Syn and syn-ack retransmissions are permitted above */
        break;
    }

    if (!permitted) {
        LOG_FIREWALL("TCP FILTER", "on interface %u packet invalid in state %u: flags %c%c%c seq %u ack %u\n",
                     filter_config.interface, conn_state, pkt.syn ? 'S' : '-', pkt.ack ? 'A' : '-',
                     pkt.fin ? 'F' : '-', pkt.seq, pkt.ack_seq);
        return false;
    }

    if (!claim_if_neighbour(instance, is_internal)) {
        return false;
    }

    fw_tcp_record_packet(internal_tracking, external_tracking, &pkt);
    __atomic_store_n(&instance->timestamp, now_ns, __ATOMIC_RELAXED);
    return true;
}

static void filter(void)
{
    bool transmitted = false;
    bool returned = false;
    bool reprocess = true;
    while (reprocess) {
        while (!net_queue_empty_active(&rx_queue)) {
            net_buff_desc_t buffer;
            int err = net_dequeue_active(&rx_queue, &buffer);
            assert(!err);

            uintptr_t pkt_vaddr = (uintptr_t)(net_config.rx_data.vaddr + buffer.io_or_offset);
            ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt_vaddr + IPV4_HDR_OFFSET);

            if (!packet_valid(&buffer, ip_hdr)) {
                err = net_enqueue_free(&rx_queue, buffer);
                assert(!err);
                returned = true;
                LOG_FIREWALL("TCP FILTER", "on interface %u dropping fragment or truncated packet\n",
                             filter_config.interface);
                continue;
            }

            tcp_hdr_t *tcp_hdr = (tcp_hdr_t *)(pkt_vaddr + transport_layer_offset(ip_hdr));

            uint16_t rule_id = 0;
            fw_tcp_instance_t *instance = NULL;
            bool is_internal = false;
            fw_action_t action = fw_tcp_filter_find_action(&filter_state, ip_hdr->src_ip, tcp_hdr->src_port,
                                                           ip_hdr->dst_ip, tcp_hdr->dst_port, &rule_id,
                                                           filter_config.interface, &instance, &is_internal);

            bool forward_packet = false;
            switch (action) {
            case FILTER_ACT_ALLOW:
                forward_packet = true;
                break;
            case FILTER_ACT_CONNECT:
            case FILTER_ACT_ESTABLISHED:
                forward_packet = track_packet(ip_hdr, tcp_hdr, rule_id, instance, is_internal);
                break;
            default:
                break;
            }

            if (forward_packet) {
                /* Reset the checksum as it's recalculated in hardware */
#ifdef NETWORK_HW_HAS_CHECKSUM
                tcp_hdr->check = 0;
#endif
                err = fw_enqueue(&router_queue, &buffer);
                assert(!err);
                transmitted = true;

                if (action == FILTER_ACT_ALLOW || action == FILTER_ACT_CONNECT) {
                    LOG_FIREWALL("TCP FILTER", "on interface %u transmitting via rule %u: (ip %s, port %u) -> (ip %s, "
                                 "port %u)\n", filter_config.interface, rule_id, ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                                 htons(tcp_hdr->src_port), ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1), htons(tcp_hdr->dst_port));
                } else {
                    LOG_FIREWALL("TCP FILTER", "on interface %u transmitting tracked connection of rule %u: (ip %s, "
                                 "port %u) -> (ip %s, port %u)\n", filter_config.interface, rule_id, ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                                 htons(tcp_hdr->src_port), ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1), htons(tcp_hdr->dst_port));
                }
            } else {
                /* Return the buffer to the rx virtualiser */
                err = net_enqueue_free(&rx_queue, buffer);
                assert(!err);
                returned = true;

                LOG_FIREWALL("TCP FILTER", "on interface %u dropping via rule %u: (ip %s, port %u) -> (ip %s, port %u)\n",
                             filter_config.interface, rule_id, ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                             htons(tcp_hdr->src_port), ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1), htons(tcp_hdr->dst_port));
            }
        }

        net_request_signal_active(&rx_queue);
        reprocess = false;

        if (!net_queue_empty_active(&rx_queue)) {
            net_cancel_signal_active(&rx_queue);
            reprocess = true;
        }
    }

    if (returned) {
        microkit_deferred_notify(net_config.rx.id);
    }

    if (transmitted) {
        microkit_notify(filter_config.router.ch);
    }
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    switch (microkit_msginfo_get_label(msginfo)) {
    case FILTER_SET_DEFAULT_ACTION: {
        fw_action_t action = microkit_mr_get(FILTER_SET_DEFAULT_ARG_ACTION);

        LOG_FIREWALL("TCP FILTER", "on interface %u changing default action from %u to %u\n",
                     filter_config.interface, filter_state.rule_table->rules[DEFAULT_ACTION_IDX].action, action);

        fw_filter_err_t err = fw_filter_update_default_action(&filter_state, action);
        assert(err == FILTER_ERR_OKAY);

        microkit_mr_set(FILTER_RET_ERR, err);
        return microkit_msginfo_new(0, 1);
    }
    case FILTER_ADD_RULE: {
        fw_action_t action = microkit_mr_get(FILTER_ADD_ARG_ACTION);
        uint32_t src_ip = microkit_mr_get(FILTER_ADD_ARG_SRC_IP);
        uint16_t src_port = microkit_mr_get(FILTER_ADD_ARG_SRC_PORT);
        uint32_t dst_ip = microkit_mr_get(FILTER_ADD_ARG_DST_IP);
        uint16_t dst_port = microkit_mr_get(FILTER_ADD_ARG_DST_PORT);
        uint8_t src_subnet = microkit_mr_get(FILTER_ADD_ARG_SRC_SUBNET);
        uint8_t dst_subnet = microkit_mr_get(FILTER_ADD_ARG_DST_SUBNET);
        bool src_port_any = microkit_mr_get(FILTER_ADD_ARG_SRC_ANY_PORT);
        bool dst_port_any = microkit_mr_get(FILTER_ADD_ARG_DST_ANY_PORT);

        /* TCP filter does not support this action */
        if (action == 0 || action > FW_FILTER_NUM_ACTIONS || !filter_config.webserver.actions[action - 1]) {
            microkit_mr_set(FILTER_RET_ERR, FILTER_ERR_UNSUPPORTED_ACTION);
            return microkit_msginfo_new(0, 1);
        }

        uint16_t rule_id = 0;
        fw_filter_err_t err = fw_filter_add_rule(&filter_state, src_ip, src_port, dst_ip, dst_port, src_subnet,
                                                 dst_subnet, src_port_any, dst_port_any, action, &rule_id);

        LOG_FIREWALL("TCP FILTER", "on interface %u create rule %u: (ip %s, mask %u, port %u, any_port %u) - (%s) -> "
                     "(ip %s, mask %u, port %u, any_port %u): %s\n", filter_config.interface, rule_id,
                     ipaddr_to_string(src_ip, ip_addr_buf0), src_subnet, htons(src_port), src_port_any, fw_filter_action_str[action],
                     ipaddr_to_string(dst_ip, ip_addr_buf1), dst_subnet, htons(dst_port), dst_port_any, fw_filter_err_str[err]);

        microkit_mr_set(FILTER_RET_ERR, err);
        microkit_mr_set(FILTER_RET_RULE_ID, rule_id);
        return microkit_msginfo_new(0, 2);
    }
    case FILTER_DEL_RULE: {
        uint16_t rule_id = microkit_mr_get(FILTER_DELETE_ARG_RULE_ID);
        fw_filter_err_t err = fw_filter_remove_rule(&filter_state, rule_id);

        LOG_FIREWALL("TCP FILTER", "on interface %u remove rule id %u: %s\n", filter_config.interface, rule_id,
                     fw_filter_err_str[err]);

        microkit_mr_set(FILTER_RET_ERR, err);
        return microkit_msginfo_new(0, 1);
    }
    default:
        LOG_FIREWALL_ERR("TCP FILTER", "on interface %u, unknown request %lu on channel %u\n", filter_config.interface,
                         microkit_msginfo_get_label(msginfo), ch);
        break;
    }

    return microkit_msginfo_new(0, 0);
}

void notified(microkit_channel ch)
{
    if (ch == net_config.rx.id) {
        filter();
    } else if (ch == timer_config.driver_id) {
        now_ns = sddf_timer_time_now(timer_config.driver_id);
        remove_timed_out_instances();
        sddf_timer_set_timeout(timer_config.driver_id, TCP_TICK_NS);
    } else {
        LOG_FIREWALL_ERR("TCP FILTER", "on interface %u, received notification on unknown channel: %d!\n",
                         filter_config.interface, ch);
    }
}

void init(void)
{
    assert(net_config_check_magic((void *)&net_config));

    net_queue_init(&rx_queue, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
                   net_config.rx.num_buffers);

    fw_queue_init(&router_queue, filter_config.router.queue.vaddr, sizeof(net_buff_desc_t),
                  filter_config.router.capacity);

    fw_filter_state_init(&filter_state, filter_config.webserver.rules.vaddr, filter_config.rule_id_bitmap.vaddr,
                         filter_config.webserver.rules_capacity, filter_config.internal_instances.vaddr,
                         filter_config.external_instances, filter_config.instances_capacity,
                         filter_config.initial_rules, filter_config.num_initial_rules,
                         filter_config.num_external_instances);
    filter_state.remove_instances = remove_rule_instances;
    now_ns = sddf_timer_time_now(timer_config.driver_id);

    /* Seed the hash from the start up time and interface */
    uint64_t seed = (now_ns ^ ((uint64_t)filter_config.interface << 56)) * 0x9E3779B97F4A7C15ull;
    fw_tcp_table(filter_state.internal_instances_table)->hash_seed = (uint32_t)(seed >> 32);

    sddf_timer_set_timeout(timer_config.driver_id, TCP_TICK_NS);
}
