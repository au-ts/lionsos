/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <lions/firewall/config.h>
#include <lions/firewall/filter.h>
#include <lions/firewall/routing.h>
#include <lwip/pbuf.h>
#include <sddf/network/lib_sddf_lwip.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * Firewall webserver configuration structure.
 */
extern fw_webserver_config_t fw_config;

/**
 * Firewall webserver data structure tracking the routing table and filter rules
 * for each network interface.
 */
typedef struct fw_webserver_interface_state {
    fw_filter_state_t filter_states[FW_MAX_FILTERS];
    uint16_t num_rules[FW_MAX_FILTERS];
} fw_webserver_interface_state_t;

extern fw_webserver_interface_state_t webserver_state[FW_MAX_INTERFACES];

/**
 * Checks whether the pbuf contains an ARP request. All ARP requests and
 * responses in the firewall are handled by the ARP components, thus the
 * webserver is not permitted to send ARP request packets out directly. Instead
 * all ARP request packets are intercepted and enqueued in the ARP request queue
 * shared with the routing component.
 *
 * @param p pbuf to check.
 *
 * @return whether pbuf contains an outgoing ARP request packet.
 */
bool mpfirewall_intercept_arp(struct pbuf *p);

/**
 * Converts a pbuf containing an ARP request packet into a fw_arp_request_t and
 * enqueues the request in the ARP requester component queue. All ARP requests
 * and responses in the firewall are handled by the ARP components, thus the
 * webserver is not permitted to send ARP request packets out directly. Instead
 * all ARP request packets are intercepted and enqueued in the ARP request queue
 * shared with the routing component.
 *
 * @param p pbuf containing ARP request packet.
 *
 * @return error status of operation.
 */
net_sddf_err_t mpfirewall_handle_arp(struct pbuf *p);

/**
 * Processes the ARP response queue shared with the ARP requester component. ARP
 * responses are converted into ARP ethernet packets and input into the LWIP
 * network interface. Dequeues all ARP responses until queue is empty or the lib
 * sDDF LWIP memory pool runs out.
 */
void mpfirewall_process_arp(void);

/**
 * Processes the Rx active packet queue shared with the routing component.
 * Dequeues all available packets and inputs them into the LWIP network
 * interface until queue is empty or the lib sDDF LWIP memory pool runs out.
 */
void mpfirewall_process_rx(void);

/**
 * Handles the sending of notifications to the ARP requester and firewall Rx
 * virtualiser. Must be invoked at the end of each event handling loop and
 * initialisation to ensure neighbouring components are scheduled to process
 * their queues.
 */
void mpfirewall_handle_notify(void);

/**
 * Initialises firewall webserver data structures containing shared data with
 * filter and routing components.
 */
void init_firewall_webserver(void);
