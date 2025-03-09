#pragma once

#include <microkit.h>
#include <stdbool.h>
#include <stdint.h>
#include <sddf/resources/common.h>
#include <sddf/resources/device.h>

#define LIONSOS_FIREWALL_MAX_FILTERS 61
#define LIONS_FIREWALL_MAGIC_LEN 8
static char LIONS_FIREWALL_MAGIC[LIONS_FIREWALL_MAGIC_LEN] = { 'L', 'i', 'o', 'n', 's', 'O', 'S', 0x3 };

static bool firewall_config_check_magic(void *config)
{
    char *magic = (char *)config;
    for (int i = 0; i < LIONS_FIREWALL_MAGIC_LEN; i++) {
        if (magic[i] != LIONS_FIREWALL_MAGIC[i]) {
            return false;
        }
    }

    return true;
}

typedef struct firewall_connection_resource {
    region_resource_t free_queue;
    region_resource_t active_queue;
    uint16_t num_buffers;
    uint8_t id;
} firewall_connection_resource_t;

typedef struct firewall_filter_info {
    firewall_connection_resource_t conn;
    region_resource_t data;
} firewall_filter_info_t;

typedef struct arp_router_connection_resource {
    region_resource_t arp_queue;
    region_resource_t arp_cache;
    uint8_t id;
} arp_router_connection_resource_t;

typedef struct arp_requester_config {
    char magic[LIONS_FIREWALL_MAGIC_LEN];
    arp_router_connection_resource_t router;
    uint8_t mac_addr[ETH_HWADDR_LEN];
    uint32_t ip;
} arp_requester_config_t;

typedef struct arp_responder_config {
    char magic[LIONS_FIREWALL_MAGIC_LEN];
    uint32_t ip;
    uint8_t mac_addr[ETH_HWADDR_LEN];
} arp_responder_config_t;

typedef struct router_config {
    char magic[LIONS_FIREWALL_MAGIC_LEN];
    region_resource_t packet_queue;
    arp_router_connection_resource_t router;
    uint8_t mac_addr[ETH_HWADDR_LEN];
    firewall_filter_info_t filters[LIONSOS_FIREWALL_MAX_FILTERS];
    uint16_t num_filters;
} router_config_t;

typedef struct filter_config {
    char magic[LIONS_FIREWALL_MAGIC_LEN];
    firewall_connection_resource_t conn;
    region_resource_t data;
    uint16_t protocol;
} filter_config_t;