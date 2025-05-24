/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <os/sddf.h>
#include <stdint.h>
#include <py/runtime.h>
#include <sddf/util/string.h>
#include <lions/firewall/config.h>
#include <lions/firewall/filter.h>
#include <lions/firewall/protocols.h>
#include <lions/firewall/routing.h>
#include <sddf/util/printf.h>
#include "firewall_structs.h"
#include <sddf/network/util.h>

extern firewall_webserver_config_t firewall_config;

typedef struct webserver_state {
    routing_table_t router_info[FIREWALL_NUM_INTERFACES];
} webserver_state_t;

webserver_state_t webserver_state = {0};

webserver_interface_t interfaces[] = {
    {
        .mac = "cc:ee:cc:dd:ee:ff",
        .cidr = "192.168.1.10/24",
    },
    {
        .mac = "77:88:22:33:44:55",
        .cidr = "192.168.1.11/16",
    },
};

webserver_routing_entry_t routing_table[256] = {
    {
        .id = 0,
        .destination = "192.168.1.0/24",
        .gateway = "NULL",
        .interface = 0,
    },
    {
        .id = 1,
        .destination = "192.168.2.0/24",
        .gateway = "NULL",
        .interface = 1,
    },
    {
        .id = 2,
        .destination = "0.0.0.0/0",
        .gateway = "192.168.2.1",
        .interface = 1,
    },
};

size_t n_routes = 3;
size_t next_route_id = 3;

#define INVALID 0

// @kwinter: Find a better way to do this initialisation.
void firewall_webserver_init(void)
{
    sddf_dprintf("Initialsing webserver state.\n");
    for (int i = 0; i < FIREWALL_NUM_INTERFACES; i++) {
        sddf_dprintf("This is the vaddr of the routing table[%d]: 0x%p\n", i, firewall_config.routers[i].routing_table.vaddr);
        routing_entry_t default_entry = {true, ROUTING_OUT_EXTERNAL, 0, 0, 0, 0};
        routing_table_init(&webserver_state.router_info[i], default_entry, firewall_config.routers[i].routing_table.vaddr,
            firewall_config.routers[i].routing_table_capacity);
    }
}

// TODO: Replace this ip_to_int

/* Convert the character string in "ip" into an unsigned integer.

This assumes that an unsigned integer contains at least 32 bits. */

static uint32_t ip_to_int (const char *ip)
{
    /* The return value. */
    unsigned v = 0;
    /* The count of the number of bytes processed. */
    int i;
    /* A pointer to the next digit to process. */
    const char * start;

    start = ip;
    for (i = 0; i < 4; i++) {
        /* The digit being processed. */
        char c;
        /* The value of this byte. */
        int n = 0;
        while (1) {
            c = * start;
            start++;
            if (c >= '0' && c <= '9') {
                n *= 10;
                n += c - '0';
            }
            /* We insist on stopping at "." if we are still parsing
               the first, second, or third numbers. If we have reached
               the end of the numbers, we will allow any character. */
            else if ((i < 3 && c == '.') || i == 3) {
                break;
            }
            else {
                return INVALID;
            }
        }
        if (n >= 256) {
            return INVALID;
        }
        v *= 256;
        v += n;
    }
    return v;
}

STATIC mp_obj_t interface_get_mac(mp_obj_t interface_idx_in) {
    uint64_t interface_idx = mp_obj_get_int(interface_idx_in);
    if (interface_idx >= 2) {
        mp_raise_OSError(-1);
        return mp_const_none;
    }
    char *mac = interfaces[interface_idx].mac;
    return mp_obj_new_str(mac, sddf_strlen(mac));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(interface_get_mac_obj, interface_get_mac);

STATIC mp_obj_t interface_get_cidr(mp_obj_t interface_idx_in) {
    uint64_t interface_idx = mp_obj_get_int(interface_idx_in);
    if (interface_idx >= 2) {
        mp_raise_OSError(-1);
        return mp_const_none;
    }
    char *cidr = interfaces[interface_idx].cidr;
    return mp_obj_new_str(cidr, sddf_strlen(cidr));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(interface_get_cidr_obj, interface_get_cidr);

STATIC mp_obj_t interface_set_cidr(mp_obj_t interface_idx_in, mp_obj_t new_cidr_in) {
    uint64_t interface_idx = mp_obj_get_int(interface_idx_in);
    const char *new_cidr = mp_obj_str_get_str(new_cidr_in);

    if (interface_idx >= 2) {
        mp_raise_OSError(-1);
        return mp_const_none;
    }
    if (sddf_strlen(new_cidr) > MAX_CIDR_LEN) {
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    sddf_strncpy(interfaces[interface_idx].cidr, new_cidr, sddf_strlen(new_cidr) + 1);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(interface_set_cidr_obj, interface_set_cidr);

STATIC mp_obj_t route_add(mp_uint_t n_args, const mp_obj_t *args) {
    sddf_dprintf("In route add\n");
    if (n_args != 5) {
        sddf_dprintf("Wrong amount of args supplied!\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    sddf_dprintf("Getting all our values\n");
    uint32_t iface = mp_obj_get_int(args[0]);
    if (iface != 0 && iface != 1) {
        sddf_dprintf("Wrong interface id supplied!\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    const char *destination = mp_obj_str_get_str(args[1]);
    uint32_t subnet = mp_obj_get_int(args[2]);
    const char *next_hop = mp_obj_str_get_str(args[3]);
    uint32_t num_hops = mp_obj_get_int(args[4]);

    uint32_t dest_ip = (ip_to_int(destination));
    uint32_t next_hop_ip = (ip_to_int(next_hop));

    seL4_SetMR(ROUTER_ARG_IP, dest_ip);
    seL4_SetMR(ROUTER_ARG_SUBNET, subnet);
    seL4_SetMR(ROUTER_ARG_NEXT_HOP, next_hop_ip);
    seL4_SetMR(ROUTER_ARG_NUM_HOPS, num_hops);

    microkit_msginfo msginfo = microkit_ppcall(firewall_config.routers[iface].routing_ch, microkit_msginfo_new(FIREWALL_ADD_ROUTE, 4));
    uint32_t err = seL4_GetMR(FILTER_RET_ERR);
    if(err) {
        mp_raise_OSError(-1);
        return mp_obj_new_int_from_uint(err);
    }
    uint32_t route_id = seL4_GetMR(FILTER_RET_RULE_ID);
    return mp_obj_new_int_from_uint(route_id);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR(route_add_obj, 5, route_add);

STATIC mp_obj_t route_delete(mp_obj_t route_id_in, mp_obj_t interface) {
    uint32_t route_id = mp_obj_get_int(route_id_in);

    const char *interface_var = mp_obj_str_get_str(interface);
    uint32_t iface = 0;
    sddf_dprintf("This is the interface: %s\n", interface_var);
    if (!sddf_strcmp(interface_var, "external")) {
        sddf_dprintf("We got an external filter\n");
        iface = 0;
    } else if (!sddf_strcmp(interface_var, "internal")) {
        sddf_dprintf("We got an internal filter\n");
        iface = 1;
    } else {
        sddf_dprintf("ERR| rule_delete: Invalid interface\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    seL4_SetMR(ROUTER_ARG_ROUTE_ID, route_id);
    microkit_msginfo msginfo = microkit_ppcall(firewall_config.routers[iface].routing_ch, microkit_msginfo_new(FIREWALL_DEL_ROUTE, 1));
    uint32_t err = seL4_GetMR(FILTER_RET_ERR);
    if(err) return mp_obj_new_int_from_uint(err);
    uint32_t route_id_ret = seL4_GetMR(FILTER_RET_RULE_ID);
    return mp_obj_new_int_from_uint(route_id_ret);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(route_delete_obj, route_delete);

STATIC mp_obj_t route_count(mp_obj_t interface) {

    const char *interface_var = mp_obj_str_get_str(interface);
    // Count all routes on both interfaces.
    uint32_t n_routes = 0;

    uint32_t iface = 0;
    sddf_dprintf("This is the interface: %s\n", interface_var);
    if (!sddf_strcmp(interface_var, "external")) {
        sddf_dprintf("We got an external filter\n");
        iface = 0;
    } else if (!sddf_strcmp(interface_var, "internal")) {
        sddf_dprintf("We got an internal filter\n");
        iface = 1;
    } else {
        sddf_dprintf("ERR| rule_delete: Invalid interface\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    sddf_dprintf("This is the routing table capacity: %d\n", webserver_state.router_info[iface].capacity);
    for (int i = 0; i < firewall_config.routers->routing_table_capacity; i++) {
        if (webserver_state.router_info[iface].entries[i].valid ) {
            n_routes++;
        }
    }

    sddf_dprintf("We found this many routes %u for iface %u\n", n_routes, iface);

    return mp_obj_new_int_from_uint(n_routes);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(route_count_obj, route_count);

STATIC mp_obj_t route_get_nth(mp_obj_t route_idx_in, mp_obj_t interface) {
    const char *interface_var = mp_obj_str_get_str(interface);

    uint32_t iface = 0;
    sddf_dprintf("This is the interface: %s\n", interface_var);
    if (!sddf_strcmp(interface_var, "external")) {
        sddf_dprintf("We got an external filter\n");
        iface = 0;
    } else if (!sddf_strcmp(interface_var, "internal")) {
        sddf_dprintf("We got an internal filter\n");
        iface = 1;
    } else {
        sddf_dprintf("ERR| rule_delete: Invalid interface\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    uint64_t route_idx = mp_obj_get_int(route_idx_in);

    if (route_idx >= webserver_state.router_info[iface].capacity) {
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    routing_entry_t *routes = webserver_state.router_info[iface].entries;

    uint32_t count = 0;
    for (int i = 0; i < webserver_state.router_info[iface].capacity; i++) {
        if (count == route_idx && routes[i].valid) {
            mp_obj_t tuple[5];
            tuple[0] = mp_obj_new_int_from_uint(i);
            char dest_buf[16];
            char dest_ip = ipaddr_to_string(routes[i].ip, dest_buf);
            tuple[1] = mp_obj_new_str(dest_buf, sddf_strlen(dest_buf));
            tuple[2] = mp_obj_new_int_from_uint(routes[i].subnet);
            char hop_buf[16];
            char hop_ip = ipaddr_to_string(routes[i].next_hop, hop_buf);
            tuple[3] = mp_obj_new_str(hop_buf, sddf_strlen(hop_buf));
            tuple[4] = mp_obj_new_int_from_uint(routes[i].num_hops);
            return mp_obj_new_tuple(5, tuple);
        } else if (count == route_idx && !routes[i].valid) {
            sddf_dprintf("Was not a valid route! -- %d\n", route_idx);
            break;
        } else if (routes[i].valid) {
            // Increment on valid routes
            count++;
        }
    }

    // @kwinter: Change the front end to print an error on getting a 0
    sddf_dprintf("ERR| route_get_nth: Could not find a valid routes for supplied route index.\n");
    return mp_obj_new_int_from_uint(0);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(route_get_nth_obj, route_get_nth);

STATIC mp_obj_t rule_add(mp_uint_t n_args, const mp_obj_t *args) {
    if (n_args != 9) {
        sddf_dprintf("Wrong amount of args supplied!\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    const char *protocol_var = mp_obj_str_get_str(args[0]);

    int protocol_id = 0;

    if (!sddf_strcmp(protocol_var, "icmp")) {
        protocol_id = IPV4_PROTO_ICMP;
    } else if (!sddf_strcmp(protocol_var, "udp")) {
        protocol_id = IPV4_PROTO_UDP;
    } else if (!sddf_strcmp(protocol_var, "tcp")) {
        protocol_id = IPV4_PROTO_TCP;
    } else {
        sddf_dprintf("ERR| rule_add: Unsuppored protocol\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    int filter_var = mp_obj_get_int(args[1]);
    if (filter_var != 0 && filter_var != 1) {
        sddf_dprintf("Incorrect filter value!\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }
    const char *src_ip_var = mp_obj_str_get_str(args[2]);
    int src_port_var = mp_obj_get_int(args[3]);
    int src_subnet_var = mp_obj_get_int(args[4]);
    const char *dst_ip_var = mp_obj_str_get_str(args[5]);
    int dst_port_var = mp_obj_get_int(args[6]);
    int dst_subnet_var = mp_obj_get_int(args[7]);
    int action_var = mp_obj_get_int(args[8]);
    // Find the filter that implements this protocol in this direction.
    for (int i = 0; i < firewall_config.num_filters; i++) {
        if (firewall_config.filters[i].protocol == protocol_id && firewall_config.filter_iface_id[i] == filter_var) {
            // Convert all the strings to integers.
            uint32_t src_ip_addr = HTONS(ip_to_int(src_ip_var));
            uint32_t dst_ip_addr = HTONS(ip_to_int(dst_ip_var));
            // Choosing random value outside of enum range
            // int action_val = 9;
            // if (sddf_strcmp(action_var, "Allow")) {
            //     action_val = FILTER_ACT_ALLOW;
            // } else if (sddf_strcmp(action_var, "Drop")) {
            //     action_val = FILTER_ACT_DROP;
            // } else if (sddf_strcmp(action_var, "Connect")) {
            //     action_val = FILTER_ACT_CONNECT;
            // }

            seL4_SetMR(FILTER_ARG_ACTION, action_var);
            seL4_SetMR(FILTER_ARG_SRC_IP, src_ip_addr);
            seL4_SetMR(FILTER_ARG_SRC_PORT, src_port_var);
            seL4_SetMR(FILTER_ARG_DST_IP, dst_ip_addr);
            seL4_SetMR(FILTER_ARG_DST_PORT, dst_port_var);
            seL4_SetMR(FILTER_ARG_SRC_SUBNET, src_subnet_var);
            seL4_SetMR(FILTER_ARG_DST_SUBNET, dst_subnet_var);
            // TODO: Add in another field to configure the "any port" in both src + dest.
            seL4_SetMR(FILTER_ARG_SRC_ANY_PORT, 0);
            seL4_SetMR(FILTER_ARG_DST_ANY_PORT, 0);
            microkit_msginfo msginfo = microkit_ppcall(firewall_config.filters[i].ch, microkit_msginfo_new(FIREWALL_ADD_RULE, 10));
            uint32_t err = seL4_GetMR(FILTER_RET_ERR);
            if(err) return mp_obj_new_int_from_uint(err);
            uint32_t rule_id = seL4_GetMR(FILTER_RET_RULE_ID);
            return mp_obj_new_int_from_uint(rule_id);
        }
    }

    sddf_dprintf("Could not find the appropriate filter!\n");
    mp_raise_OSError(-1);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR(rule_add_obj, 9, rule_add);

STATIC mp_obj_t rule_delete(mp_obj_t rule_id_in, mp_obj_t protocol, mp_obj_t filter) {
    uint32_t rule_id = mp_obj_get_int(rule_id_in);

    const char *protocol_var = mp_obj_str_get_str(protocol);
    int protocol_id = 0;
    if (!sddf_strcmp(protocol_var, "icmp")) {
        sddf_dprintf("We got an icmp filter\n");
        protocol_id = IPV4_PROTO_ICMP;
    } else if (!sddf_strcmp(protocol_var, "udp")) {
        sddf_dprintf("We got a udp filter\n");
        protocol_id = IPV4_PROTO_UDP;
    } else if (!sddf_strcmp(protocol_var, "tcp")) {
        sddf_dprintf("We got a tcp filter\n");
        protocol_id = IPV4_PROTO_TCP;
    } else {
        sddf_dprintf("ERR| rule_delete: Unsupported protocol.\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    sddf_dprintf("This is the value of protocol id: %d\n", protocol_id);

    const char *filter_var = mp_obj_str_get_str(filter);
    int iface = 4;
    sddf_dprintf("This is the filter: %s\n", filter_var);
    if (!sddf_strcmp(filter_var, "external")) {
        sddf_dprintf("We got an external filter\n");
        iface = 0;
    } else if (!sddf_strcmp(filter_var, "internal")) {
        sddf_dprintf("We got an internal filter\n");
        iface = 1;
    } else {
        sddf_dprintf("ERR| rule_delete: Invalid interface\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    // Find the list of rules to read from
    firewall_rule_t *rules = NULL;
    firewall_webserver_filter_config_t filter_config;

    for (int i = 0; i < FIREWALL_MAX_FILTERS; i++) {
        if (firewall_config.filters[i].protocol == protocol_id && firewall_config.filter_iface_id[i] == iface) {
            rules = (firewall_rule_t *) firewall_config.filters[i].rules.vaddr;
            filter_config = firewall_config.filters[i];
        }
    }

    if (rules == NULL) {
        sddf_dprintf("ERR| rule_delete: Unable to find protocol on supplied interface\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }


    if (rules[rule_id].valid) {
        sddf_dprintf("We found our index %d, rule was valid\n", rule_id);
        // We found our rule index, delete it.
        seL4_SetMR(FILTER_ARG_RULE_ID, rule_id);
        microkit_msginfo msginfo = microkit_ppcall(filter_config.ch, microkit_msginfo_new(FIREWALL_DEL_RULE, 2));
        uint32_t err = seL4_GetMR(FILTER_RET_ERR);
        return mp_obj_new_int_from_uint(err);
    }

    // int index_cnt = 0;

    // for (int i = 0; i < firewall_config.rules_capacity; i++) {
    //     sddf_dprintf("This is index: %d --- This is index_cnt: %d --- This is rule_id: %d\n", i, index_cnt, rule_id)
    //     if (index_cnt == rule_id && rules[i].valid) {
    //         sddf_dprintf("We found our index, rule was valid\n");
    //         // We found our rule index, delete it.
    //         seL4_SetMR(FILTER_ARG_RULE_ID, i);
    //         microkit_msginfo msginfo = microkit_ppcall(filter_config.ch, microkit_msginfo_new(FIREWALL_DEL_RULE, 1));
    //         uint32_t err = seL4_GetMR(FILTER_RET_ERR);
    //         return mp_obj_new_int_from_uint(err);
    //     } else if (index_cnt == rule_id && !rules[i].valid){
    //         sddf_dprintf("We found our index, but the rule was invalid for some reason?\n")
    //     } else if (rules[i].valid) {
    //         index_cnt++;
    //     }
    // }

    // If we get here, it means that our index was invalid.
    sddf_dprintf("ERR| rule_delete: Invalid index to delete: %d\n", rule_id);
    mp_raise_OSError(-1);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(rule_delete_obj, rule_delete);

STATIC mp_obj_t rule_count(mp_obj_t protocol, mp_obj_t filter) {

    const char *protocol_var = mp_obj_str_get_str(protocol);
    int protocol_id = 0;
    if (!sddf_strcmp(protocol_var, "icmp")) {
        protocol_id = IPV4_PROTO_ICMP;
    } else if (!sddf_strcmp(protocol_var, "udp")) {
        protocol_id = IPV4_PROTO_UDP;
    } else if (!sddf_strcmp(protocol_var, "tcp")) {
        protocol_id = IPV4_PROTO_TCP;
    } else {
        sddf_dprintf("ERR| rule_count: Unsuppored protocol.\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    const char *filter_var = mp_obj_str_get_str(filter);
    int iface = 4;
    if (!sddf_strcmp(filter_var, "external")) {
        iface = 0;
    } else if (!sddf_strcmp(filter_var, "internal")) {
        iface = 1;
    } else {
        sddf_dprintf("ERR| rule_count: Invalid interface\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    // Find the list of rules to read from
    firewall_rule_t *rules = NULL;

    for (int i = 0; i < firewall_config.num_filters; i++) {
        if (firewall_config.filters[i].protocol == protocol_id && firewall_config.filter_iface_id[i] == iface) {
            rules = (firewall_rule_t *) firewall_config.filters[i].rules.vaddr;
        }
    }

    if (rules == NULL) {
        sddf_dprintf("ERR| rule_count: Unable to find any rules!\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    int index_cnt = 0;

    for (int i = 0; i < firewall_config.rules_capacity; i++) {
        if (rules[i].valid) {
            index_cnt++;
        }
    }

    return mp_obj_new_int_from_uint(index_cnt);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(rule_count_obj, rule_count);

STATIC mp_obj_t filter_set_default_action(mp_obj_t protocol, mp_obj_t filter, mp_obj_t action) {
    const char *protocol_var = mp_obj_str_get_str(protocol);
    int protocol_id = 0;
    if (!sddf_strcmp(protocol_var, "icmp")) {
        protocol_id = IPV4_PROTO_ICMP;
    } else if (!sddf_strcmp(protocol_var, "udp")) {
        protocol_id = IPV4_PROTO_UDP;
    } else if (!sddf_strcmp(protocol_var, "tcp")) {
        protocol_id = IPV4_PROTO_TCP;
    } else {
        sddf_dprintf("ERR| rule_count: Unsuppored protocol.\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    const char *filter_var = mp_obj_str_get_str(filter);
    int iface = 4;
    if (!sddf_strcmp(filter_var, "external")) {
        iface = 0;
    } else if (!sddf_strcmp(filter_var, "internal")) {
        iface = 1;
    } else {
        sddf_dprintf("ERR| rule_count: Invalid interface\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    uint32_t action_id = mp_obj_get_int(action);

    if (action_id != 1 && action_id != 2) {
        sddf_dprintf("ERR| filter_set_default_action: Invalid action id: %d\n", action_id);
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    for (int i = 0; i < firewall_config.num_filters; i++) {
        if (firewall_config.filters[i].protocol == protocol_id && firewall_config.filter_iface_id[i] == iface) {
            seL4_SetMR(FILTER_ARG_ACTION, action_id);
            microkit_msginfo msginfo = microkit_ppcall(firewall_config.filters[i].ch, microkit_msginfo_new(FIREWALL_SET_DEFAULT_ACTION, 1));
            uint32_t err = seL4_GetMR(FILTER_RET_ERR);
            if (err) {
                sddf_dprintf("We receieved an error when trying to do set default action via ppcall\n");
            }
            // If we were successful, update the default action in our own data structures.
            firewall_config.filters[i].default_action = action_id;
            return mp_obj_new_int_from_uint(err);
        }
    }
    // @kwinter: Change the front end to print an error on getting a 0
    sddf_dprintf("ERR| filter_set_default_action: Could not find a matchong protocol on specified interface.\n");
    return mp_obj_new_int_from_uint(0);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(filter_set_default_action_obj, filter_set_default_action);

STATIC mp_obj_t filter_get_default_action(mp_obj_t protocol, mp_obj_t filter) {
    const char *protocol_var = mp_obj_str_get_str(protocol);
    int protocol_id = 0;
    if (!sddf_strcmp(protocol_var, "icmp")) {
        protocol_id = IPV4_PROTO_ICMP;
    } else if (!sddf_strcmp(protocol_var, "udp")) {
        protocol_id = IPV4_PROTO_UDP;
    } else if (!sddf_strcmp(protocol_var, "tcp")) {
        protocol_id = IPV4_PROTO_TCP;
    } else {
        sddf_dprintf("ERR| rule_count: Unsuppored protocol.\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    const char *filter_var = mp_obj_str_get_str(filter);
    int iface = 4;
    if (!sddf_strcmp(filter_var, "external")) {
        iface = 0;
    } else if (!sddf_strcmp(filter_var, "internal")) {
        iface = 1;
    } else {
        sddf_dprintf("ERR| rule_count: Invalid interface\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    for (int i = 0; i < firewall_config.num_filters; i++) {
        if (firewall_config.filters[i].protocol == protocol_id && firewall_config.filter_iface_id[i] == iface) {
            return mp_obj_new_int_from_uint((firewall_config.filters[i].default_action));
        }
    }
    // @kwinter: Change the front end to print an error on getting a 0
    sddf_dprintf("ERR| filter_get_default_action: Could not find a matchong protocol on specified interface.\n");
    return mp_obj_new_int_from_uint(0);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(filter_get_default_action_obj, filter_get_default_action);

STATIC mp_obj_t rule_get_nth(mp_obj_t protocol, mp_obj_t filter, mp_obj_t rule_idx_in) {
    const char *protocol_var = mp_obj_str_get_str(protocol);
    int protocol_id = 0;
    if (!sddf_strcmp(protocol_var, "icmp")) {
        protocol_id = IPV4_PROTO_ICMP;
    } else if (!sddf_strcmp(protocol_var, "udp")) {
        protocol_id = IPV4_PROTO_UDP;
    } else if (!sddf_strcmp(protocol_var, "tcp")) {
        protocol_id = IPV4_PROTO_TCP;
    } else {
        sddf_dprintf("ERR| rule_get_nth: Unsuppored protocol.\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    const char *filter_var = mp_obj_str_get_str(filter);
    int iface = 4;
    if (!sddf_strcmp(filter_var, "external")) {
        iface = 0;
    } else if (!sddf_strcmp(filter_var, "internal")) {
        iface = 1;
    } else {
        sddf_dprintf("ERR| rule_get_nth: Invalid interface\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    // Find the list of rules to read from
    firewall_rule_t *rules = NULL;

    for (int i = 0; i < FIREWALL_MAX_FILTERS; i++) {
        if (firewall_config.filters[i].protocol == protocol_id && firewall_config.filter_iface_id[i] == iface) {
            rules = (firewall_rule_t *) firewall_config.filters[i].rules.vaddr;
        }
    }

    if (rules == NULL) {
        sddf_dprintf("ERR| rule_get_nth: Unable to find protocol on supplied interface\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    uint64_t rule_idx = mp_obj_get_int(rule_idx_in);

    if (rule_idx >= firewall_config.rules_capacity) {
        sddf_dprintf("ERR| rule_get_nth: Rule index exceeds bounds of rule list\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    uint32_t index_cnt = 0;
    bool found_entry = false;
    int i = 0;
    for (i = 0; i < firewall_config.rules_capacity; i++) {
        if (index_cnt == rule_idx && rules[i].valid) {
            // We found our rule index
            found_entry = true;
            break;
        } else if (rules[i].valid){
            index_cnt++;
        }
    }

    if (!found_entry) {
        sddf_dprintf("ERR| rule_get_nth: Rule index exceeds bounds of rule list\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    firewall_rule_t rule = rules[i];

    if (!rule.valid) {
        sddf_dprintf("Invalid rule\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }
    mp_obj_t tuple[10];
    // We use the rule index as the rule ID in the instances. Use the same
    // index as the ID here.
    tuple[0] = mp_obj_new_int_from_uint(i);
    char buf[16];
    char src_ip = ipaddr_to_string(rule.src_ip, buf);
    tuple[1] = mp_obj_new_str(buf, sddf_strlen(buf));
    tuple[2] = mp_obj_new_int_from_uint(rule.src_port);
    char dst_ip = ipaddr_to_string(rule.dst_ip, buf);
    tuple[3] = mp_obj_new_str(buf, sddf_strlen(buf));
    tuple[4] = mp_obj_new_int_from_uint(rule.dst_port);
    tuple[5] = mp_obj_new_int_from_uint(rule.src_subnet);
    tuple[6] = mp_obj_new_int_from_uint(rule.dst_subnet);
    tuple[7] = mp_obj_new_int_from_uint(rule.src_port_any);
    tuple[8] = mp_obj_new_int_from_uint(rule.dst_port_any);
    if (rule.action == FILTER_ACT_ALLOW) {
        tuple[9] = mp_obj_new_str("ALLOW", 5);
    } else if (rule.action == FILTER_ACT_DROP) {
        tuple[9] = mp_obj_new_str("DROP", 4);
    } else {
        tuple[9] = mp_obj_new_str("CONNECT", 7);
    }

    return mp_obj_new_tuple(10, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(rule_get_nth_obj, rule_get_nth);

STATIC const mp_rom_map_elem_t lions_firewall_module_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_lions_firewall) },
    { MP_ROM_QSTR(MP_QSTR_interface_mac_get), MP_ROM_PTR(&interface_get_mac_obj) },
    { MP_ROM_QSTR(MP_QSTR_interface_cidr_get), MP_ROM_PTR(&interface_get_cidr_obj) },
    { MP_ROM_QSTR(MP_QSTR_interface_cidr_set), MP_ROM_PTR(&interface_set_cidr_obj) },
    { MP_ROM_QSTR(MP_QSTR_route_add), MP_ROM_PTR(&route_add_obj) },
    { MP_ROM_QSTR(MP_QSTR_route_delete), MP_ROM_PTR(&route_delete_obj) },
    { MP_ROM_QSTR(MP_QSTR_route_count), MP_ROM_PTR(&route_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_route_get_nth), MP_ROM_PTR(&route_get_nth_obj) },
    { MP_ROM_QSTR(MP_QSTR_rule_add), MP_ROM_PTR(&rule_add_obj) },
    { MP_ROM_QSTR(MP_QSTR_rule_delete), MP_ROM_PTR(&rule_delete_obj) },
    { MP_ROM_QSTR(MP_QSTR_rule_count), MP_ROM_PTR(&rule_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_rule_get_nth), MP_ROM_PTR(&rule_get_nth_obj) },
    { MP_ROM_QSTR(MP_QSTR_filter_get_default_action), MP_ROM_PTR(&filter_get_default_action_obj) },
    { MP_ROM_QSTR(MP_QSTR_filter_set_default_action), MP_ROM_PTR(&filter_set_default_action_obj) },
};
STATIC MP_DEFINE_CONST_DICT(lions_firewall_module_globals, lions_firewall_module_globals_table);

const mp_obj_module_t lions_firewall_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&lions_firewall_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_lions_firewall, lions_firewall_module);
