#pragma once

#include <os/sddf.h>
#include <stdint.h>
#include <stdbool.h>
#include <sddf/util/util.h>
#include <sddf/network/util.h>
#include <lions/firewall/common.h>
#include <lions/firewall/array_functions.h>

/**
 * Stores original source and destination corresponding to a NAT ephemeral port.
 * This is an endpoint independent mapping since only source address and port are used.
 */
typedef struct fw_nat_port_mapping {
    /* original source ip of traffic */
    uint32_t src_ip;
    /* original source port of traffic */
    uint16_t src_port;
} fw_nat_port_mapping_t;

typedef struct fw_nat_port_table {
    uint16_t size;
    fw_nat_port_mapping_t mappings[];
} fw_nat_port_table_t;
