/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <os/sddf.h>
#include <stdbool.h>
#include <stdint.h>
#include <sddf/resources/common.h>
#include <sddf/resources/device.h>
#include <sddf/network/config.h>
#include <sddf/network/constants.h>

#define FW_MAX_FW_CLIENTS 61
#define FW_MAX_FILTERS 61

#define FW_NUM_ARP_REQUESTER_CLIENTS 2
#define FW_NUM_INTERFACES 2

#define FW_DEBUG_OUTPUT 1

typedef struct fw_connection_resource {
    region_resource_t queue;
    uint16_t capacity;
    uint8_t ch;
} fw_connection_resource_t;

typedef struct fw_data_connection_resource {
    fw_connection_resource_t conn;
    device_region_resource_t data;
} fw_data_connection_resource_t;

typedef struct fw_net_virt_tx_config {
    /* Interface traffic is transmitted out of */
    uint8_t interface;
    fw_data_connection_resource_t active_clients[FW_MAX_FW_CLIENTS];
    uint8_t num_active_clients;
    fw_data_connection_resource_t free_clients[FW_MAX_FW_CLIENTS];
    uint8_t num_free_clients;
} fw_net_virt_tx_config_t;

typedef struct fw_net_virt_rx_config {
    /* Interface traffic is received from */
    uint8_t interface;
    /* Eth-type of traffic to be routed to each client */
    uint16_t active_client_ethtypes[SDDF_NET_MAX_CLIENTS];
    /* Sub-type of traffic to be routed to each client. If ethtype == IPv4, this
    field holds IPv4 protocol numbers. If ethtype == ARP, this field holds ARP
    opcodes */
    uint16_t active_client_subtypes[SDDF_NET_MAX_CLIENTS];
    fw_connection_resource_t free_clients[FW_MAX_FW_CLIENTS];
    uint8_t num_free_clients;
} fw_net_virt_rx_config_t;

typedef struct fw_arp_connection {
    region_resource_t request;
    region_resource_t response;
    uint16_t capacity;
    uint8_t ch;
} fw_arp_connection_t;

typedef struct fw_arp_requester_config {
    /* Interface traffic is received from */
    uint8_t interface;
    /* MAC address of output interface */
    uint8_t mac_addr[ETH_HWADDR_LEN];
    /* IP address of output interface */
    uint32_t ip;
    fw_arp_connection_t arp_clients[FW_NUM_ARP_REQUESTER_CLIENTS];
    uint8_t num_arp_clients;
    region_resource_t arp_cache;
    uint16_t arp_cache_capacity;
} fw_arp_requester_config_t;

typedef struct fw_arp_responder_config {
    /* Interface traffic is received from */
    uint8_t interface;
    /* MAC address of input and output interface */
    uint8_t mac_addr[ETH_HWADDR_LEN];
    /* IP address of input and output interface */
    uint32_t ip;
} fw_arp_responder_config_t;

typedef struct fw_webserver_router_config {
    uint8_t routing_ch;
    region_resource_t routing_table;
    uint16_t routing_table_capacity;
} fw_webserver_router_config_t;

typedef struct fw_router_config {
    /* Interface traffic is received from */
    uint8_t interface;
    /* MAC address of output interface */
    uint8_t mac_addr[ETH_HWADDR_LEN];
    /* IP address of output interface */
    uint32_t ip;
    /* Subnet bits of output interface */
    uint32_t subnet;
    /* IP address of input interface */
    uint32_t in_ip;
    fw_connection_resource_t rx_free;
    fw_connection_resource_t rx_active;
    fw_connection_resource_t tx_active;
    region_resource_t data;
    fw_arp_connection_t arp_queue;
    region_resource_t arp_cache;
    uint16_t arp_cache_capacity;
    region_resource_t packet_queue;
    fw_webserver_router_config_t webserver;
    fw_connection_resource_t icmp_module;
    fw_connection_resource_t filters[FW_MAX_FILTERS];
    uint8_t num_filters;
} fw_router_config_t;

typedef struct fw_icmp_module_config {
    /* IP address of interfaces */
    uint32_t ips[FW_NUM_INTERFACES];
    fw_connection_resource_t routers[FW_NUM_INTERFACES];
    fw_connection_resource_t filters[FW_MAX_FILTERS];
    uint8_t num_interfaces;
    uint8_t num_filters;
} fw_icmp_module_config_t;

typedef struct fw_webserver_filter_config {
    uint16_t protocol;
    uint8_t ch;
    uint8_t default_action;
    region_resource_t rules;
    uint16_t rules_capacity;
} fw_webserver_filter_config_t;

typedef struct fw_filter_config {
    /* Interface traffic is received from */
    uint8_t interface;
    uint16_t instances_capacity;
    fw_connection_resource_t router;
    fw_webserver_filter_config_t webserver;
    region_resource_t internal_instances;
    region_resource_t external_instances;
    region_resource_t rule_id_bitmap;
    fw_connection_resource_t icmp_module;
} fw_filter_config_t;

typedef struct fw_webserver_interface_config {
    /* MAC address of interface */
    uint8_t mac_addr[ETH_HWADDR_LEN];
    /* IP address of interface */
    uint32_t ip;
    fw_webserver_router_config_t router;
    fw_webserver_filter_config_t filters[FW_MAX_FILTERS];
    uint8_t num_filters;
} fw_webserver_interface_config_t;

typedef struct fw_webserver_config {
    /* Interface traffic is received from */
    uint8_t interface;
    fw_connection_resource_t rx_active;
    region_resource_t data;
    fw_connection_resource_t rx_free;
    fw_arp_connection_t arp_queue;
    fw_webserver_interface_config_t interfaces[FW_NUM_INTERFACES];
    uint8_t num_interfaces;
} fw_webserver_config_t;
