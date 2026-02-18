// Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause
#pragma once

#include <sddf/resources/common.h>
#include <sddf/resources/device.h>
#include <stdbool.h>
#include <stdint.h>

#define ETH_HWADDR_LEN 6
#define SDDF_NET_MAX_CLIENTS 64
#define FW_MAX_FW_CLIENTS 61
#define FW_MAX_FILTERS 61
#define FW_NUM_ARP_REQUESTER_CLIENTS 2
#define FW_MAX_INTERFACES 4
#define FW_INTERFACE_NAME_LEN 32
#define FW_DEBUG_OUTPUT 1

typedef struct fw_arp_responder_config {
    uint8_t interface;
    uint8_t mac_addr[ETH_HWADDR_LEN];
    uint32_t ip;
} fw_arp_responder_config_t;

typedef struct fw_arp_connection {
    region_resource_t request;
    region_resource_t response;
    uint16_t capacity;
    uint8_t ch;
} fw_arp_connection_t;

typedef struct fw_arp_requester_config {
    uint8_t interface;
    uint8_t mac_addr[ETH_HWADDR_LEN];
    uint32_t ip;
    fw_arp_connection_t arp_clients[FW_NUM_ARP_REQUESTER_CLIENTS];
    uint8_t num_arp_clients;
    region_resource_t arp_cache;
    uint16_t arp_cache_capacity;
} fw_arp_requester_config_t;

typedef struct fw_connection_resource {
    region_resource_t queue;
    uint16_t capacity;
    uint8_t ch;
} fw_connection_resource_t;

typedef struct fw_data_connection_resource {
    fw_connection_resource_t conn;
    device_region_resource_t data;
} fw_data_connection_resource_t;

typedef struct fw_filter_config {
    uint8_t interface;
    uint8_t default_action;
    uint16_t instances_capacity;
    fw_connection_resource_t router;
    region_resource_t internal_instances;
    region_resource_t external_instances;
    region_resource_t rules;
    uint16_t rules_capacity;
    region_resource_t rule_id_bitmap;
} fw_filter_config_t;

typedef struct fw_icmp_module_config {
    uint32_t ips[FW_MAX_INTERFACES];
    fw_connection_resource_t router;
    uint8_t num_interfaces;
} fw_icmp_module_config_t;

typedef struct fw_net_virt_rx_config {
    uint8_t interface;
    uint16_t active_client_ethtypes[SDDF_NET_MAX_CLIENTS];
    uint16_t active_client_subtypes[SDDF_NET_MAX_CLIENTS];
    fw_connection_resource_t free_clients[FW_MAX_FW_CLIENTS];
    uint8_t num_free_clients;
} fw_net_virt_rx_config_t;

typedef struct fw_net_virt_tx_config {
    uint8_t interface;
    fw_data_connection_resource_t active_clients[FW_MAX_FW_CLIENTS];
    uint8_t num_active_clients;
    fw_data_connection_resource_t free_clients[FW_MAX_FW_CLIENTS];
    uint8_t num_free_clients;
} fw_net_virt_tx_config_t;

typedef struct fw_router_interface {
    fw_connection_resource_t rx_free;
    fw_connection_resource_t tx_active[FW_MAX_INTERFACES];
    region_resource_t data;
    fw_arp_connection_t arp_queue;
    region_resource_t arp_cache;
    uint16_t arp_cache_capacity;
    fw_connection_resource_t filters[FW_MAX_FILTERS];
    uint8_t num_filters;
    uint8_t mac_addr[ETH_HWADDR_LEN];
    uint32_t ip;
    uint32_t subnet;
} fw_router_interface_t;

typedef struct fw_webserver_filter_config {
    uint16_t protocol;
    uint8_t ch;
    region_resource_t rules;
    uint16_t rules_capacity;
} fw_webserver_filter_config_t;

typedef struct fw_webserver_interface_config {
    uint8_t mac_addr[ETH_HWADDR_LEN];
    uint32_t ip;
    fw_webserver_filter_config_t filters[FW_MAX_FILTERS];
    uint8_t num_filters;
    uint8_t name[FW_INTERFACE_NAME_LEN];
} fw_webserver_interface_config_t;

typedef struct fw_webserver_router_config {
    uint8_t routing_ch;
    region_resource_t routing_table;
    uint16_t routing_table_capacity;
} fw_webserver_router_config_t;

typedef struct fw_router_config {
    uint8_t num_interfaces;
    uint8_t webserver_interface;
    fw_router_interface_t interfaces[FW_MAX_INTERFACES];
    region_resource_t packet_queue;
    uint16_t packet_waiting_capacity;
    fw_webserver_router_config_t webserver;
    fw_connection_resource_t icmp_module;
    fw_connection_resource_t webserver_rx;
} fw_router_config_t;

typedef struct fw_webserver_config {
    uint8_t interface;
    fw_connection_resource_t rx_active;
    region_resource_t data;
    fw_connection_resource_t rx_free;
    fw_arp_connection_t arp_queue;
    fw_webserver_router_config_t router;
    fw_webserver_interface_config_t interfaces[FW_MAX_INTERFACES];
    uint8_t num_interfaces;
} fw_webserver_config_t;
