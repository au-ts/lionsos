/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <stdint.h>
#include <py/runtime.h>
#include <sddf/util/string.h>
#include <lions/firewall/config.h>
#include <lions/firewall/filter.h>
#include <lions/firewall/protocols.h>
#include <lions/firewall/routing.h>

#include "firewall_structs.h"

extern firewall_webserver_config_t firewall_config;

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

static char *ipaddr_to_string(uint32_t s_addr, char *buf, int buflen)
{
    char inv[3], *rp;
    uint8_t *ap, rem, n, i;
    int len = 0;

    rp = buf;
    ap = (uint8_t *)&s_addr;
    for (n = 0; n < 4; n++) {
        i = 0;
        do {
            rem = *ap % (uint8_t)10;
            *ap /= (uint8_t)10;
            inv[i++] = (char)('0' + rem);
        } while (*ap);
        while (i--) {
            if (len++ >= buflen) {
                return NULL;
            }
            *rp++ = inv[i];
        }
        if (len++ >= buflen) {
            return NULL;
        }
        *rp++ = '.';
        ap++;
    }
    *--rp = 0;
    return buf;
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

STATIC mp_obj_t route_add(mp_obj_t destination_in, mp_obj_t gateway_in, mp_obj_t interface_in) {
    const char *destination = mp_obj_str_get_str(destination_in);
    const char *gateway = gateway_in != mp_const_none ? mp_obj_str_get_str(gateway_in) : NULL;
    uint64_t interface = mp_obj_get_int(interface_in);

    webserver_routing_entry_t *route = &routing_table[n_routes++];
    route->id = next_route_id++;
    sddf_strncpy(route->destination, destination, sddf_strlen(destination) + 1);
    if (gateway != NULL) {
        sddf_strncpy(route->gateway, gateway, sddf_strlen(gateway) + 1);
    } else {
        route->gateway[0] = '\0';
    }
    route->interface = interface;

    return mp_obj_new_int_from_uint(route->id);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(route_add_obj, route_add);

STATIC mp_obj_t route_delete(mp_obj_t route_id_in) {
    uint64_t route_id = mp_obj_get_int(route_id_in);

    size_t table_idx = UINT64_MAX;
    for (size_t i = 0; i < n_routes; i++) {
        if (route_id == routing_table[i].id) {
            table_idx = i;
            break;
        }
    }
    if (table_idx == UINT64_MAX) {
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    routing_table[table_idx] = routing_table[n_routes - 1];
    n_routes--;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(route_delete_obj, route_delete);

STATIC mp_obj_t route_count(void) {
    return mp_obj_new_int_from_uint(n_routes);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(route_count_obj, route_count);

STATIC mp_obj_t route_get_nth(mp_obj_t route_idx_in) {
    uint64_t route_idx = mp_obj_get_int(route_idx_in);

    if (route_idx >= n_routes) {
        mp_raise_OSError(-1);
        return mp_const_none;
    }
    webserver_routing_entry_t *route = &routing_table[route_idx];

    mp_obj_t tuple[4];
    tuple[0] = mp_obj_new_int_from_uint(route->id);
    tuple[1] = mp_obj_new_str(route->destination, sddf_strlen(route->destination));
    tuple[2] = route->gateway[0] != '\0' ? mp_obj_new_str(route->gateway, sddf_strlen(route->gateway)) : mp_const_none;
    tuple[3] = mp_obj_new_int_from_uint(route->interface);

    return mp_obj_new_tuple(4, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(route_get_nth_obj, route_get_nth);

STATIC mp_obj_t rule_add(mp_obj_t protocol, mp_obj_t filter, mp_obj_t src_ip, mp_obj_t src_port,
                        mp_obj_t src_subnet, mp_obj_t dst_ip, mp_obj_t dst_port, mp_obj_t dst_subnet,
                        mp_obj_t action) {

    const char *protocol_var = mp_obj_str_get_str(protocol);
    int protocol_id = 0;
    if (sddf_strcmp(protocol_var, "icmp")) {
        protocol_id = IPV4_PROTO_ICMP;
    } else if (sddf_strcmp(protocol_var, "udp")) {
        protocol_id = IPV4_PROTO_UDP;
    } else if (sddf_strcmp(protocol_var, "tcp")) {
        protocol_id = IPV4_PROTO_TCP;
    } else {
        printf("ERR| rule_add: Unsuppored protocol\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    const char *filter_var = mp_obj_str_get_str(filter);
    int iface = 4;
    if (sddf_strcmp(filter_var, "external")) {
        iface = 1;
    } else if (sddf_strcmp(filter_var, "internal")) {
        iface = 2;
    } else {
        printf("ERR| rule_add: Invalid interface\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }
    const char *src_ip_var = mp_obj_str_get_str(src_ip);
    uint16_t src_port_var = mp_obj_get_int(src_port);
    uint8_t src_subnet_var = mp_obj_get_int(src_subnet);
    const char *dst_ip_var = mp_obj_str_get_str(dst_ip);
    uint16_t dst_port_var = mp_obj_get_int(dst_port);
    uint8_t dst_subnet_var = mp_obj_get_int(dst_subnet);
    const char *action_var = mp_obj_str_get_str(action);

    // Find the filter that implements this protocol in this direction.
    for (int i = 0; i < FIREWALL_MAX_FILTERS; i++) {
        if (firewall_config.filters[i].protocol == protocol_id) {
            // Convert all the strings to integers.
            uint32_t src_ip_addr = ip_to_int(src_ip_var);
            uint32_t dst_ip_addr = ip_to_int(dst_ip_var);
            // Choosing random value outside of enum range
            int action_val = 9;
            if (sddf_strcmp(action_var, "Allow")) {
                action_val = FILTER_ACT_ALLOW;
            } else if (sddf_strcmp(action_var, "Drop")) {
                action_val = FILTER_ACT_DROP;
            } else if (sddf_strcmp(action_var, "Connect")) {
                action_val = FILTER_ACT_CONNECT;
            }

            seL4_SetMR(FILTER_ARG_ACTION, action_val);
            seL4_SetMR(FILTER_ARG_SRC_IP, src_ip_addr);
            seL4_SetMR(FILTER_ARG_SRC_PORT, src_port_var);
            seL4_SetMR(FILTER_ARG_DST_IP, dst_ip_addr);
            seL4_SetMR(FILTER_ARG_DST_PORT, dst_port_var);
            seL4_SetMR(FILTER_ARG_SRC_SUBNET, src_subnet_var);
            seL4_SetMR(FILTER_ARG_DST_SUBNET, dst_subnet_var);
            // TODO: Add in another field to configure the "any port" in both src + dest.
            seL4_SetMR(FILTER_ARG_SRC_ANY_PORT, 0);
            seL4_SetMR(FILTER_ARG_DST_ANY_PORT, 0);

            microkit_msginfo msginfo = microkit_ppcall(firewall_config.filters[i].ch, microkit_msginfo_new(FIREWALL_ADD_RULE, 9));
            uint32_t err = seL4_GetMR(FILTER_RET_ERR);
            if(err) return mp_obj_new_int_from_uint(err);
            uint32_t rule_id = seL4_GetMR(FILTER_RET_RULE_ID);
            return mp_obj_new_int_from_uint(rule_id);
        }
    }

    mp_raise_OSError(-1);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(rule_add_obj, rule_add);

STATIC mp_obj_t rule_delete(mp_obj_t rule_id_in, mp_obj_t protocol, mp_obj_t filter) {
    uint64_t rule_id = mp_obj_get_int(rule_id_in);

    const char *protocol_var = mp_obj_str_get_str(protocol);
    int protocol_id = 0;
    if (sddf_strcmp(protocol_var, "icmp")) {
        protocol_id = IPV4_PROTO_ICMP;
    } else if (sddf_strcmp(protocol_var, "udp")) {
        protocol_id = IPV4_PROTO_UDP;
    } else if (sddf_strcmp(protocol_var, "tcp")) {
        protocol_id = IPV4_PROTO_TCP;
    } else {
        printf("ERR| rule_delete: Unsuppored protocol.\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    const char *filter_var = mp_obj_str_get_str(filter);
    int iface = 4;
    if (sddf_strcmp(filter_var, "external")) {
        iface = 1;
    } else if (sddf_strcmp(filter_var, "internal")) {
        iface = 2;
    } else {
        printf("ERR| rule_delete: Invalid interface\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    // Find the list of rules to read from
    firewall_rule_t *rules = NULL;

    for (int i = 0; i < FIREWALL_MAX_FILTERS; i++) {
        if (firewall_config.filters[i].protocol == protocol_id) {
            rules = (firewall_rule_t *) firewall_config.filters[i].rules.vaddr;
        }
    }

    int index_cnt = 0;

    for (int i = 0; i < firewall_config.rules_capacity; i++) {
        if (index_cnt == rule_id && rules[i].valid) {
            // We found our rule index, delete it.
            seL4_SetMR(FILTER_ARG_RULE_ID, i);
            microkit_msginfo msginfo = microkit_ppcall(firewall_config.filters[i].ch, microkit_msginfo_new(FIREWALL_DEL_RULE, 1));
            uint32_t err = seL4_GetMR(FILTER_RET_ERR);
            return mp_obj_new_int_from_uint(err);
        } else if (rules[i].valid){
            index_cnt++;
        }
    }

    // If we get here, it means that our index was invalid.
    printf("ERR| rule_delete: Invalid index to delete\n");
    mp_raise_OSError(-1);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(rule_delete_obj, rule_delete);

STATIC mp_obj_t rule_count(mp_obj_t protocol, mp_obj_t filter) {
    const char *protocol_var = mp_obj_str_get_str(protocol);
    int protocol_id = 0;
    if (sddf_strcmp(protocol_var, "icmp")) {
        protocol_id = IPV4_PROTO_ICMP;
    } else if (sddf_strcmp(protocol_var, "udp")) {
        protocol_id = IPV4_PROTO_UDP;
    } else if (sddf_strcmp(protocol_var, "tcp")) {
        protocol_id = IPV4_PROTO_TCP;
    } else {
        printf("ERR| rule_delete: Unsuppored protocol.\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    const char *filter_var = mp_obj_str_get_str(filter);
    int iface = 4;
    if (sddf_strcmp(filter_var, "external")) {
        iface = 1;
    } else if (sddf_strcmp(filter_var, "internal")) {
        iface = 2;
    } else {
        printf("ERR| rule_delete: Invalid interface\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    // Find the list of rules to read from
    firewall_rule_t *rules = NULL;

    for (int i = 0; i < FIREWALL_MAX_FILTERS; i++) {
        if (firewall_config.filters[i].protocol == protocol_id) {
            rules = (firewall_rule_t *) firewall_config.filters[i].rules.vaddr;
        }
    }

    int index_cnt = 0;

    for (int i = 0; i < firewall_config.rules_capacity; i++) {
        if (rules[i].valid) {
            // We found our rule index, delete it.
            index_cnt++;
        }
    }
    return mp_obj_new_int_from_uint(index_cnt);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(rule_count_obj, rule_count);

STATIC mp_obj_t filter_default_action(mp_obj_t protocol, mp_obj_t filter) {
    const char *protocol_var = mp_obj_str_get_str(protocol);
    int protocol_id = 0;
    if (sddf_strcmp(protocol_var, "icmp")) {
        protocol_id = IPV4_PROTO_ICMP;
    } else if (sddf_strcmp(protocol_var, "udp")) {
        protocol_id = IPV4_PROTO_UDP;
    } else if (sddf_strcmp(protocol_var, "tcp")) {
        protocol_id = IPV4_PROTO_TCP;
    } else {
        printf("ERR| filter_default_action: Unsuppored protocol.\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    const char *filter_var = mp_obj_str_get_str(filter);
    int iface = 4;
    if (sddf_strcmp(filter_var, "external")) {
        iface = 1;
    } else if (sddf_strcmp(filter_var, "internal")) {
        iface = 2;
    } else {
        printf("ERR| filter_default_action: Invalid interface\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    for (int i = 0; i < FIREWALL_MAX_FILTERS; i++) {
        if (firewall_config.filters[i].protocol == protocol_id) {
            return mp_obj_new_int_from_uint((firewall_config.filters[i].default_action));
        }
    }
    // @kwinter: Change the front end to print an error on getting a 0
    printf("ERR| filter_default_action: Could not find a matchong protocol on specified interface.\n");
    return mp_obj_new_int_from_uint(0);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(filter_default_action_obj, filter_default_action);

STATIC mp_obj_t rule_get_nth(mp_obj_t protocol, mp_obj_t filter, mp_obj_t rule_idx_in) {
    const char *protocol_var = mp_obj_str_get_str(protocol);
    int protocol_id = 0;
    if (sddf_strcmp(protocol_var, "icmp")) {
        protocol_id = IPV4_PROTO_ICMP;
    } else if (sddf_strcmp(protocol_var, "udp")) {
        protocol_id = IPV4_PROTO_UDP;
    } else if (sddf_strcmp(protocol_var, "tcp")) {
        protocol_id = IPV4_PROTO_TCP;
    } else {
        printf("ERR| rule_get_nth: Unsuppored protocol.\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    const char *filter_var = mp_obj_str_get_str(filter);
    int iface = 4;
    if (sddf_strcmp(filter_var, "external")) {
        iface = 1;
    } else if (sddf_strcmp(filter_var, "internal")) {
        iface = 2;
    } else {
        printf("ERR| rule_get_nth: Invalid interface\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    // Find the list of rules to read from
    firewall_rule_t *rules = NULL;

    for (int i = 0; i < FIREWALL_MAX_FILTERS; i++) {
        if (firewall_config.filters[i].protocol == protocol_id) {
            rules = (firewall_rule_t *) firewall_config.filters[i].rules.vaddr;
        }
    }

    if (rules == NULL) {
        printf("ERR| rule_get_nth: Unable to find protocol on supplied interface\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    uint64_t rule_idx = mp_obj_get_int(rule_idx_in);

    if (rule_idx >= firewall_config.rules_capacity) {
        printf("ERR| rule_get_nth: Rule index exceeds bounds of rule list\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    uint32_t index_cnt = 0;
    bool found_entry = false;
    for (int i = 0; i < firewall_config.rules_capacity; i++) {
        if (index_cnt == rule_idx && rules[i].valid) {
            // We found our rule index
            found_entry = true;
            break;
        } else if (rules[i].valid){
            index_cnt++;
        }
    }

    if (!found_entry) {
        printf("ERR| rule_get_nth: Rule index exceeds bounds of rule list\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    firewall_rule_t rule = rules[index_cnt];

    if (!rule.valid) {
        printf("Invalid rule\n");
        mp_raise_OSError(-1);
        return mp_const_none;
    }


    mp_obj_t tuple[10];
    // We use the rule index as the rule ID in the instances. Use the same
    // index as the ID here.
    tuple[0] = mp_obj_new_int_from_uint(rule_idx);
    char buf[16];
    char src_ip = ipaddr_to_string(rule.src_ip, buf, 16);
    tuple[1] = mp_obj_new_str(src_ip, sddf_strlen(src_ip));
    tuple[2] = mp_obj_new_int_from_uint(rule.src_port);
    char dst_ip = ipaddr_to_string(rule.dst_ip, buf, 16);
    tuple[3] = mp_obj_new_str(dst_ip, sddf_strlen(dst_ip));
    tuple[4] = mp_obj_new_int_from_uint(rule.dst_port);
    tuple[5] = mp_obj_new_int_from_uint(rule.src_subnet);
    tuple[6] = mp_obj_new_int_from_uint(rule.dst_subnet);
    if (rule.action == FILTER_ACT_ALLOW) {
       tuple[7] = mp_obj_new_str("ALLOW", 5);
    } else if (rule.action == FILTER_ACT_DROP) {
        tuple[7] = mp_obj_new_str("DROP", 4);
    } else {
        tuple[7] = mp_obj_new_str("CONNECT", 7);
    }
    tuple[8] = mp_obj_new_int_from_uint(rule.src_port_any);
    tuple[9] = mp_obj_new_int_from_uint(rule.dst_port_any);

    return mp_obj_new_tuple(10, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(rule_get_nth_obj, rule_get_nth);

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
    { MP_ROM_QSTR(MP_QSTR_filter_default_action), MP_ROM_PTR(&filter_default_action_obj) },
};
STATIC MP_DEFINE_CONST_DICT(lions_firewall_module_globals, lions_firewall_module_globals_table);

const mp_obj_module_t lions_firewall_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&lions_firewall_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_lions_firewall, lions_firewall_module);
