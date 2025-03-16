#pragma once

#include <microkit.h>
#include <stdbool.h>
#include <stdint.h>
#include <sddf/resources/common.h>
#include <sddf/resources/device.h>
#include <sddf/network/constants.h>

#define FIREWALL_MAX_FIREWALL_CLIENTS 61
#define FIREWALL_MAX_FILTERS 61

#define FIREWALL_MAX_CACHE_ENTRIES 512

#define FIREWALL_DEBUG_OUTPUT 1

typedef struct firewall_connection_resource {
    region_resource_t queue;
    uint16_t capacity;
    uint8_t ch;
} firewall_connection_resource_t;

typedef struct firewall_data_connection_resource {
    firewall_connection_resource_t conn;
    device_region_resource_t data;
} firewall_data_connection_resource_t;

typedef struct firewall_net_virt_tx_config {
    firewall_data_connection_resource_t active_clients[FIREWALL_MAX_FIREWALL_CLIENTS];
    uint8_t num_active_clients;
    firewall_data_connection_resource_t free_clients[FIREWALL_MAX_FIREWALL_CLIENTS];
    uint8_t num_free_clients;
} firewall_net_virt_tx_config_t;

typedef struct firewall_net_virt_rx_config {
    uint16_t active_client_protocols[SDDF_NET_MAX_CLIENTS];
    firewall_connection_resource_t free_clients[FIREWALL_MAX_FIREWALL_CLIENTS];
    uint8_t num_free_clients;
} firewall_net_virt_rx_config_t;

typedef struct firewall_arp_requester_config {
    uint8_t mac_addr[ETH_HWADDR_LEN];
    uint32_t ip;
    firewall_connection_resource_t router;
    region_resource_t arp_cache;
} firewall_arp_requester_config_t;

typedef struct firewall_arp_responder_config {
    uint8_t mac_addr[ETH_HWADDR_LEN];
    uint32_t ip;
} firewall_arp_responder_config_t;

typedef struct firewall_router_webserver_config {
    firewall_data_connection_resource_t packet_forwarding;
    region_resource_t routes;
    uint8_t ch;
} firewall_router_webserver_config_t;

typedef struct firewall_router_config {
    uint8_t mac_addr[ETH_HWADDR_LEN];
    firewall_data_connection_resource_t rx_free;
    firewall_connection_resource_t tx_active;
    firewall_connection_resource_t arp_queue;
    region_resource_t arp_cache;
    region_resource_t packet_queue;
    firewall_router_webserver_config_t webserver;
    firewall_connection_resource_t filters[FIREWALL_MAX_FILTERS];
    uint16_t num_filters;
} firewall_router_config_t;

typedef struct firewall_webserver_filter_config {
    uint16_t protocol;
    region_resource_t rules;
    uint8_t ch;
} firewall_webserver_filter_config_t;

typedef struct firewall_filter_config {
    uint8_t mac_addr[ETH_HWADDR_LEN];
    firewall_connection_resource_t router;
    firewall_webserver_filter_config_t webserver;
    region_resource_t shared;
} firewall_filter_config_t;

typedef struct firewall_webserver_config {
    uint8_t mac_addr[ETH_HWADDR_LEN];
    uint32_t ip;
    firewall_router_webserver_config_t router;
    firewall_connection_resource_t rx_free;
    firewall_connection_resource_t arp_queue;
    firewall_webserver_filter_config_t filters[FIREWALL_MAX_FILTERS];
} firewall_webserver_config_t;
