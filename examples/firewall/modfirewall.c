/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include "py/runtime.h"
#include <arpa/inet.h>
// @kwinter: Remove this
#include <string.h>
#include "firewall_structs.h"
#include <lions/firewall/filter.h>
#include <lions/firewall/config.h>
#include <lions/firewall/protocols.h>

extern firewall_webserver_config_t firewall_config;

interface_t interfaces[] = {
    {
        .mac = "cc:ee:cc:dd:ee:ff",
        .cidr = "192.168.1.10/24",
    },
    {
        .mac = "77:88:22:33:44:55",
        .cidr = "192.168.1.11/16",
    },
};

routing_entry_t routing_table[256] = {
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

firewall_rule_t firewall_rules[256] = {
    {
        .id = 0,
        .protocol = "ICMP",
        .iface1 = "Anywhere",
        .iface2 = "123.456.789.0/23",
    },
    {
        .id = 1,
        .protocol = "UDP",
        .iface1 = "192.168.10.3/24",
        .iface2 = "123.456.788.10:53",
    },
    {
        .id = 2,
        .protocol = "TCP",
        .iface1 = "Anywhere",
        .iface2 = "123.456.788.23:80",
    },
};
size_t n_rules = 3;
size_t next_rule_id = 3;

STATIC mp_obj_t interface_get_mac(mp_obj_t interface_idx_in) {
    uint64_t interface_idx = mp_obj_get_int(interface_idx_in);
    if (interface_idx >= 2) {
        mp_raise_OSError(-1);
        return mp_const_none;
    }
    char *mac = interfaces[interface_idx].mac;
    return mp_obj_new_str(mac, strlen(mac));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(interface_get_mac_obj, interface_get_mac);

STATIC mp_obj_t interface_get_cidr(mp_obj_t interface_idx_in) {
    uint64_t interface_idx = mp_obj_get_int(interface_idx_in);
    if (interface_idx >= 2) {
        mp_raise_OSError(-1);
        return mp_const_none;
    }
    char *cidr = interfaces[interface_idx].cidr;
    return mp_obj_new_str(cidr, strlen(cidr));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(interface_get_cidr_obj, interface_get_cidr);

STATIC mp_obj_t interface_set_cidr(mp_obj_t interface_idx_in, mp_obj_t new_cidr_in) {
    uint64_t interface_idx = mp_obj_get_int(interface_idx_in);
    const char *new_cidr = mp_obj_str_get_str(new_cidr_in);

    if (interface_idx >= 2) {
        mp_raise_OSError(-1);
        return mp_const_none;
    }
    if (strlen(new_cidr) > MAX_CIDR_LEN) {
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    strcpy(interfaces[interface_idx].cidr, new_cidr);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(interface_set_cidr_obj, interface_set_cidr);

STATIC mp_obj_t route_add(mp_obj_t destination_in, mp_obj_t gateway_in, mp_obj_t interface_in) {
    const char *destination = mp_obj_str_get_str(destination_in);
    const char *gateway = gateway_in != mp_const_none ? mp_obj_str_get_str(gateway_in) : NULL;
    uint64_t interface = mp_obj_get_int(interface_in);

    routing_entry_t *route = &routing_table[n_routes++];
    route->id = next_route_id++;
    strcpy(route->destination, destination);
    if (gateway != NULL) {
        strcpy(route->gateway, gateway);
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
    routing_entry_t *route = &routing_table[route_idx];

    mp_obj_t tuple[4];
    tuple[0] = mp_obj_new_int_from_uint(route->id);
    tuple[1] = mp_obj_new_str(route->destination, strlen(route->destination));
    tuple[2] = route->gateway[0] != '\0' ? mp_obj_new_str(route->gateway, strlen(route->gateway)) : mp_const_none;
    tuple[3] = mp_obj_new_int_from_uint(route->interface);

    return mp_obj_new_tuple(4, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(route_get_nth_obj, route_get_nth);

STATIC mp_obj_t rule_add(mp_obj_t protocol, mp_obj_t filter, mp_obj_t src_ip, mp_obj_t src_port,
                        mp_obj_t src_subnet, mp_obj_t dest_ip, mp_obj_t dest_port, mp_obj_t dest_subnet,
                        mp_obj_t action) {

    const char *protocol_var = mp_obj_str_get_str(protocol);
    int protocol_id = 0;
    if (strcmp(protocol_var, "icmp")) {
        protocol_id = IP_TYPE_ICMP;
    } else if (strcmp(protocol_var, "udp")) {
        protocol_id = IP_TYPE_UDP;
    } else if (strcmp(protocol_var, "tcp")) {
        protocol_id = IP_TYPE_TCP;
    }

    const char *filter_var = mp_obj_str_get_str(filter);
    int iface = 4;
    if (strcmp(filter_var, "external")) {
        iface = 1;
    } else if (strcmp(filter_var, "internal")) {
        iface = 2;
    }
    const char *src_ip_var = mp_obj_str_get_str(src_ip);
    uint16_t src_port_var = mp_obj_get_int(src_port);
    uint8_t src_subnet_var = mp_obj_get_int(src_subnet);
    const char *dest_ip_var = mp_obj_str_get_str(dest_ip);
    uint16_t dest_port_var = mp_obj_get_int(dest_port);
    uint8_t dest_subnet_var = mp_obj_get_int(dest_subnet);
    const char *action = mp_obj_str_get_str(action);

    // Find the filter that implements this protocol in this direction.
    for (int i = 0; i < FIREWALL_MAX_FILTERS; i++) {
        if (firewall_config.filters[i].protocol == protocol_id &&
                firewall_config.filters[i].iface == iface) {
            // Convert all the strings to integers.
            struct in_addr src_ip_addr;
            inet_pton(AF_INET, src_ip_var, &src_ip_addr);
            struct in_addr dest_ip_addr;
            inet_pton(AF_INET, dest_ip_var, &dest_ip_addr);
            // Choosing random value outside of enum range
            int action_val = 9;
            if (strcmp(action, "Allow")) {
                action_val = ALLOW;
            } else if (strcmp(action, "Drop")) {
                action_val = DROP;
            } else if (strcmp(action, "Connect")) {
                action_val = CONNECT;
            }

            seL4_SetMR(ACTION, action_val);
            seL4_SetMR(SRC_IP, src_ip_addr.s_addr);
            seL4_SetMR(SRC_PORT, src_port_var);
            seL4_SetMR(DST_IP, dest_ip_addr.s_addr);
            seL4_SetMR(DST_PORT, dest_port_var);
            seL4_SetMR(SRC_SUBNET, src_subnet_var);
            seL4_SetMR(DST_SUBNET, dest_subnet_var);
            // TODO: Add in another field to configure the "any port" in both src + dest.
            seL4_SetMR(SRC_ANY_PORT, 0);
            seL4_SetMR(DST_ANY_PORT, 0);

            microkit_ppcall(firewall_config.filters[i].ch, microkit_msginfo_new(0, 9));
            return
        }
    }
    return mp_obj_new_int_from_uint(rule->id);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(rule_add_obj, rule_add);

STATIC mp_obj_t rule_delete(mp_obj_t rule_id_in) {
    uint64_t rule_id = mp_obj_get_int(rule_id_in);

    size_t table_idx = UINT64_MAX;
    for (size_t i = 0; i < n_rules; i++) {
        if (rule_id == firewall_rules[i].id) {
            table_idx = i;
            break;
        }
    }
    if (table_idx == UINT64_MAX) {
        mp_raise_OSError(-1);
        return mp_const_none;
    }

    firewall_rules[table_idx] = firewall_rules[n_rules - 1];
    n_rules--;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(rule_delete_obj, rule_delete);

STATIC mp_obj_t rule_count(void) {
    return mp_obj_new_int_from_uint(n_rules);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(rule_count_obj, rule_count);

STATIC mp_obj_t filter_default_action(void) {
    // @krishnan finish
    return mp_obj_new_int_from_uint(2);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(filter_default_action_obj, filter_default_action);

STATIC mp_obj_t rule_get_nth(mp_obj_t rule_idx_in) {
    // @krishnan finish
    uint64_t rule_idx = mp_obj_get_int(rule_idx_in);

    if (rule_idx >= n_rules) {
        mp_raise_OSError(-1);
        return mp_const_none;
    }
    firewall_rule_t *rule = &firewall_rules[rule_idx];

    mp_obj_t tuple[10];
    tuple[0] = mp_obj_new_int_from_uint(rule->id);
    char *dummy_src_ip = "123.456.789.0";
    uint32_t dummy_src_port = 24;
    tuple[1] = mp_obj_new_str(dummy_src_ip, strlen(dummy_src_ip));
    tuple[2] = mp_obj_new_int_from_uint(dummy_src_port);
    tuple[3] = mp_obj_new_str(dummy_src_ip, strlen(dummy_src_ip));
    tuple[4] = mp_obj_new_int_from_uint(dummy_src_port);
    tuple[5] = mp_obj_new_str(dummy_src_ip, strlen(dummy_src_ip));
    tuple[6] = mp_obj_new_str(dummy_src_ip, strlen(dummy_src_ip));
    tuple[7] = mp_obj_new_str("?", 1);
    tuple[8] = mp_obj_new_str("?", 1);
    tuple[9] = mp_obj_new_str("?", 1);

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