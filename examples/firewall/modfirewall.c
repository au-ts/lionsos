/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include <os/sddf.h>
#include <py/runtime.h>
#include <sddf/network/util.h>
#include <sddf/util/printf.h>
#include <lions/firewall/config.h>
#include <lions/firewall/filter.h>
#include <lions/firewall/protocols.h>
#include <lions/firewall/routing.h>

/* Firewall internal errors */
typedef enum {
    OS_ERR_OKAY = 0,          /* No error */
    OS_ERR_INVALID_INTERFACE, /* Invalid interface ID */
    OS_ERR_INVALID_PROTOCOL,  /* Invalid protocol number */
    OS_ERR_INVALID_ROUTE_ID,  /* Invalid route ID */
    OS_ERR_INVALID_RULE_ID,   /* Invalid rule ID */
    OS_ERR_DUPLICATE,         /* Duplicate route or rule */
    OS_ERR_CLASH,             /* Clashing route or rule */
    OS_ERR_INVALID_ARGUMENTS, /* Invalid arguments supplied */
    OS_ERR_INVALID_ROUTE_NUM, /* Invalid route number supplied to route_get_nth */
    OS_ERR_INVALID_RULE_NUM,  /* Invalid route number supplied to rule_get_nth */
    OS_ERR_OUT_OF_MEMORY,     /* Data structures full */
    OS_ERR_INTERNAL_ERROR     /* Unknown internal error */
} fw_os_err_t;

static const char *fw_os_err_str[] = {
    "Ok.",
    "Invalid interface ID supplied.",
    "No matching filter for supplied protocol number.",
    "No route matching supplied route ID.",
    "No rule matching supplied rule ID.",
    "Route or rule supplied already exists.",
    "Route or rule supplied clashes with an existing route or rule.",
    "Too many or too few arguments supplied.",
    "Route number supplied is greater than the number of routes.",
    "Rule number supplied is greater than the number of rules.",
    "Internal data structures are already at capacity.", // TODO we can check
    "Unknown internal error."
};

/* Convert a routing error to OS error */
fw_os_err_t routing_err_to_os_err(routing_err_t routing_err) {
    switch (routing_err) {
        case ROUTING_ERR_OKAY:
            return OS_ERR_OKAY;
        case ROUTING_ERR_FULL:
            return OS_ERR_OUT_OF_MEMORY;
        case ROUTING_ERR_DUPLICATE:
            return OS_ERR_DUPLICATE;
        case ROUTING_ERR_CLASH:
            return OS_ERR_CLASH;
        case ROUTING_ERR_INVALID_CHILD:
            return OS_ERR_INTERNAL_ERROR;
        case ROUTING_ERR_INVALID_ID:
            return OS_ERR_INVALID_ROUTE_ID;
        default:
            return OS_ERR_INTERNAL_ERROR;
    }
}

/* Convert a filter error to OS error */
fw_os_err_t filter_err_to_os_err(firewall_filter_err_t filter_err) {
    switch (filter_err) {
        case FILTER_ERR_OKAY:
            return OS_ERR_OKAY;
        case FILTER_ERR_FULL:
            return OS_ERR_OUT_OF_MEMORY;
        case FILTER_ERR_DUPLICATE:
            return OS_ERR_DUPLICATE;
        case FILTER_ERR_CLASH:
            return OS_ERR_CLASH;
        case FILTER_ERR_INVALID_RULE_ID:
            return OS_ERR_INVALID_RULE_ID;
        default:
            return OS_ERR_INTERNAL_ERROR;
    }
}

extern firewall_webserver_config_t firewall_config;

typedef struct webserver_net_state {
    routing_table_t routing_tables[FIREWALL_NUM_INTERFACES];
    uint16_t num_routes[FIREWALL_NUM_INTERFACES];
  
    firewall_filter_state_t filter_states[FIREWALL_NUM_INTERFACES * FIREWALL_MAX_FILTERS];
    uint16_t num_rules[FIREWALL_NUM_INTERFACES * FIREWALL_MAX_FILTERS];
} webserver_state_t;

webserver_state_t webserver_state;

void firewall_webserver_init(void) {
  for (uint8_t i = 0; i < FIREWALL_NUM_INTERFACES; i++) {
    routing_entry_t default_entry = {true, ROUTING_OUT_EXTERNAL, 0, 0, 0, 0};
    routing_table_init(&webserver_state.routing_tables[i], default_entry,
                       firewall_config.routers[i].routing_table.vaddr,
                       firewall_config.routers[i].routing_table_capacity);
  }

  for (uint8_t i = 0; i < firewall_config.num_filters; i++) {
    firewall_filter_state_init(&webserver_state.filter_states[i],
                               firewall_config.filters[i].rules.vaddr,
                               firewall_config.rules_capacity, 0, 0, 0,
                               firewall_config.filters[i].default_action);
  }

  /* Currently harcode pre-existing internal route to webserver. */
  webserver_state.num_routes[1] = 1;
}

/* Get MAC address for network interface */
STATIC mp_obj_t interface_get_mac(mp_obj_t interface_idx_in) {
    uint8_t interface_idx = mp_obj_get_int(interface_idx_in);
    if (interface_idx >= FIREWALL_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    mp_obj_t tuple[ETH_HWADDR_LEN];
    for (uint8_t i = 0; i < ETH_HWADDR_LEN; i++) {
        tuple[i] = mp_obj_new_int_from_uint(firewall_config.mac_addr[i]);
    }

    return mp_obj_new_tuple(ETH_HWADDR_LEN, tuple);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(interface_get_mac_obj, interface_get_mac);

/* Get IP address for network interface */
STATIC mp_obj_t interface_get_ip(mp_obj_t interface_idx_in) {
    uint8_t interface_idx = mp_obj_get_int(interface_idx_in);
    if (interface_idx >= FIREWALL_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    return mp_obj_new_int_from_uint(firewall_config.ip);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(interface_get_ip_obj, interface_get_ip);

/* Add a route to the routing table for a network interface */
STATIC mp_obj_t route_add(mp_uint_t n_args, const mp_obj_t *args) {
    if (n_args != 5) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_ARGUMENTS]);
        mp_raise_OSError(OS_ERR_INVALID_ARGUMENTS);
        return mp_const_none;
    }

    uint8_t interface_idx = mp_obj_get_int(args[0]);
    if (interface_idx >= FIREWALL_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    uint32_t ip = mp_obj_get_int(args[1]);
    uint8_t subnet = mp_obj_get_int(args[2]);
    uint32_t next_hop = mp_obj_get_int(args[3]);
    uint16_t num_hops = mp_obj_get_int(args[4]);

    seL4_SetMR(ROUTER_ARG_IP, ip);
    seL4_SetMR(ROUTER_ARG_SUBNET, subnet);
    seL4_SetMR(ROUTER_ARG_NEXT_HOP, next_hop);
    seL4_SetMR(ROUTER_ARG_NUM_HOPS, num_hops);

    microkit_msginfo msginfo =
        microkit_ppcall(firewall_config.routers[interface_idx].routing_ch,
                        microkit_msginfo_new(FIREWALL_ADD_ROUTE, 5));
    fw_os_err_t os_err = routing_err_to_os_err(seL4_GetMR(ROUTER_RET_ERR));
    if (os_err != OS_ERR_OKAY) {
        sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[os_err]);
        mp_raise_OSError(os_err);
        return mp_obj_new_int_from_uint(os_err);
    }

    webserver_state.num_routes[interface_idx] += 1;
    uint16_t route_id = seL4_GetMR(ROUTER_RET_ROUTE_ID);
    return mp_obj_new_int_from_uint(route_id);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_VAR(route_add_obj, 5, route_add);

/* Delete a route from the interface routing table */
STATIC mp_obj_t route_delete(mp_obj_t interface_idx_in, mp_obj_t route_id_in) {
    uint8_t interface_idx =   mp_obj_get_int(interface_idx_in);
    if (interface_idx >= FIREWALL_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    uint16_t route_id = mp_obj_get_int(route_id_in);

    seL4_SetMR(ROUTER_ARG_ROUTE_ID, route_id);
    microkit_msginfo msginfo =
        microkit_ppcall(firewall_config.routers[interface_idx].routing_ch,
                        microkit_msginfo_new(FIREWALL_DEL_ROUTE, 1));
    fw_os_err_t os_err = routing_err_to_os_err(seL4_GetMR(ROUTER_RET_ERR));
    if (os_err != OS_ERR_OKAY) {
        sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[os_err]);
        mp_raise_OSError(os_err);
        return mp_obj_new_int_from_uint(os_err);
    }

    webserver_state.num_routes[interface_idx] -= 1;
    return mp_obj_new_int_from_uint(route_id);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_2(route_delete_obj, route_delete);

/* Count the number of routes in an interface routing table */
STATIC mp_obj_t route_count(mp_obj_t interface_idx_in) {
    uint8_t interface_idx =   mp_obj_get_int(interface_idx_in);
    if (interface_idx >= FIREWALL_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    return mp_obj_new_int_from_uint(webserver_state.num_routes[interface_idx]);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(route_count_obj, route_count);

/* Return nth route in interface routing table */
STATIC mp_obj_t route_get_nth(mp_obj_t interface_idx_in,
                              mp_obj_t route_idx_in) {
    uint8_t interface_idx =   mp_obj_get_int(interface_idx_in);
    if (interface_idx >= FIREWALL_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    uint16_t route_idx = mp_obj_get_int(route_idx_in);
    if (route_idx >= webserver_state.num_routes[interface_idx] ||
        route_idx >= webserver_state.routing_tables[interface_idx].capacity) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_ROUTE_NUM]);
        mp_raise_OSError(OS_ERR_INVALID_ROUTE_NUM);
        return mp_const_none;
    }

    uint16_t valid_entries = 0;
    for (uint16_t i = 0;
        i < webserver_state.routing_tables[interface_idx].capacity; i++) {
        routing_entry_t *entry =
            (routing_entry_t
                *)(webserver_state.routing_tables[interface_idx].entries + i);
        if (!entry->valid) {
        continue;
        }

        if (valid_entries == route_idx) {
        mp_obj_t tuple[5];
        tuple[0] = mp_obj_new_int_from_uint(i);
        tuple[1] = mp_obj_new_int_from_uint(entry->ip);
        tuple[2] = mp_obj_new_int_from_uint(entry->subnet);
        tuple[3] = mp_obj_new_int_from_uint(entry->next_hop);
        tuple[4] = mp_obj_new_int_from_uint(entry->num_hops);
        return mp_obj_new_tuple(5, tuple);
        }

        valid_entries++;
    }

    sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[OS_ERR_INTERNAL_ERROR]);
    mp_raise_OSError(OS_ERR_INTERNAL_ERROR);
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_2(route_get_nth_obj, route_get_nth);

/* Add a rule to a filter on an interface */
STATIC mp_obj_t rule_add(mp_uint_t n_args, const mp_obj_t *args) {
    if (n_args != 11) {
        mp_raise_OSError(OS_ERR_INVALID_ARGUMENTS);
        return mp_const_none;
    }

    uint8_t interface_idx = mp_obj_get_int(args[0]);
    if (interface_idx >= FIREWALL_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    uint16_t protocol = mp_obj_get_int(args[1]);
    uint32_t src_ip = mp_obj_get_int(args[2]);
    uint16_t src_port = mp_obj_get_int(args[3]);
    bool src_port_any = mp_obj_get_int(args[4]);
    uint8_t src_subnet = mp_obj_get_int(args[5]);
    uint32_t dst_ip = mp_obj_get_int(args[6]);
    uint16_t dst_port = mp_obj_get_int(args[7]);
    bool dst_port_any = mp_obj_get_int(args[8]);
    uint8_t dst_subnet = mp_obj_get_int(args[9]);
    uint8_t action = mp_obj_get_int(args[10]);

    uint8_t protocol_match = firewall_config.num_filters;
    for (uint8_t i = 0; i < firewall_config.num_filters; i++) {
        if (firewall_config.filters[i].protocol == protocol &&
            firewall_config.filter_iface_id[i] == interface_idx) {
        protocol_match = i;
        break;
        }
    }

    if (protocol_match == firewall_config.num_filters) {
        sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[OS_ERR_INVALID_PROTOCOL]);
        mp_raise_OSError(OS_ERR_INVALID_PROTOCOL);
        return mp_const_none;
    }

    seL4_SetMR(FILTER_ARG_ACTION, action);
    seL4_SetMR(FILTER_ARG_SRC_IP, src_ip);
    seL4_SetMR(FILTER_ARG_SRC_PORT, src_port);
    seL4_SetMR(FILTER_ARG_SRC_ANY_PORT, src_port_any);
    seL4_SetMR(FILTER_ARG_SRC_SUBNET, src_subnet);
    seL4_SetMR(FILTER_ARG_DST_IP, dst_ip);
    seL4_SetMR(FILTER_ARG_DST_PORT, dst_port);
    seL4_SetMR(FILTER_ARG_DST_ANY_PORT, dst_port_any);
    seL4_SetMR(FILTER_ARG_DST_SUBNET, dst_subnet);

    microkit_msginfo msginfo =
        microkit_ppcall(firewall_config.filters[protocol_match].ch,
                        microkit_msginfo_new(FIREWALL_ADD_RULE, 10));
    fw_os_err_t os_err = filter_err_to_os_err(seL4_GetMR(FILTER_RET_ERR));
    if (os_err != OS_ERR_OKAY) {
        sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[os_err]);
        mp_raise_OSError(os_err);
        return mp_obj_new_int_from_uint(os_err);
    }

    uint16_t rule_id = seL4_GetMR(FILTER_RET_RULE_ID);
    webserver_state.num_rules[protocol_match] += 1;
    return mp_obj_new_int_from_uint(rule_id);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_VAR(rule_add_obj, 9, rule_add);

/* Delete a filter on an interface */
STATIC mp_obj_t rule_delete(mp_obj_t interface_idx_in, mp_obj_t rule_id_in,
                            mp_obj_t protocol_in) {
    uint8_t interface_idx = mp_obj_get_int(interface_idx_in);
    if (interface_idx >= FIREWALL_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    uint16_t rule_id = mp_obj_get_int(rule_id_in);
    uint16_t protocol = mp_obj_get_int(protocol_in);
    uint8_t protocol_match = firewall_config.num_filters;
    for (uint8_t i = 0; i < firewall_config.num_filters; i++) {
        if (firewall_config.filters[i].protocol == protocol &&
            firewall_config.filter_iface_id[i] == interface_idx) {
        protocol_match = i;
        break;
        }
    }

    if (protocol_match == firewall_config.num_filters) {
        sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[OS_ERR_INVALID_PROTOCOL]);
        mp_raise_OSError(OS_ERR_INVALID_PROTOCOL);
        return mp_const_none;
    }

    seL4_SetMR(FILTER_ARG_RULE_ID, rule_id);
    microkit_msginfo msginfo =
        microkit_ppcall(firewall_config.filters[protocol_match].ch,
                        microkit_msginfo_new(FIREWALL_DEL_RULE, 2));
    fw_os_err_t os_err = filter_err_to_os_err(seL4_GetMR(FILTER_RET_ERR));
    if (os_err != OS_ERR_OKAY) {
        sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[os_err]);
        mp_raise_OSError(os_err);
        return mp_obj_new_int_from_uint(os_err);
    }

    webserver_state.num_rules[protocol_match] -= 1;
    return mp_obj_new_int_from_uint(rule_id);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_3(rule_delete_obj, rule_delete);

/* Get number of filter rules for a filter */
STATIC mp_obj_t rule_count(mp_obj_t interface_idx_in, mp_obj_t protocol_in) {
    uint8_t interface_idx = mp_obj_get_int(interface_idx_in);
    if (interface_idx >= FIREWALL_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    uint16_t protocol = mp_obj_get_int(protocol_in);
    for (uint8_t i = 0; i < firewall_config.num_filters; i++) {
        if (firewall_config.filters[i].protocol == protocol &&
            firewall_config.filter_iface_id[i] == interface_idx) {
        return mp_obj_new_int_from_uint(webserver_state.num_rules[i]);
        }
    }

    sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[OS_ERR_INVALID_PROTOCOL]);
    mp_raise_OSError(OS_ERR_INVALID_PROTOCOL);
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_2(rule_count_obj, rule_count);

/* Set interface filter default action */
STATIC mp_obj_t filter_set_default_action(mp_obj_t interface_idx_in,
                                          mp_obj_t protocol_in,
                                          mp_obj_t action_in) {
    uint8_t interface_idx = mp_obj_get_int(interface_idx_in);
    if (interface_idx >= FIREWALL_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    uint16_t protocol = mp_obj_get_int(protocol_in);
    uint8_t action = mp_obj_get_int(action_in);
    uint8_t protocol_match = firewall_config.num_filters;
    for (uint8_t i = 0; i < firewall_config.num_filters; i++) {
        if (firewall_config.filters[i].protocol == protocol &&
            firewall_config.filter_iface_id[i] == interface_idx) {
        protocol_match = i;
        break;
        }
    }

    if (protocol_match == firewall_config.num_filters) {
        sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[OS_ERR_INVALID_PROTOCOL]);
        mp_raise_OSError(OS_ERR_INVALID_PROTOCOL);
        return mp_const_none;
    }

    seL4_SetMR(FILTER_ARG_ACTION, action);
    microkit_msginfo msginfo =
        microkit_ppcall(firewall_config.filters[protocol_match].ch,
                        microkit_msginfo_new(FIREWALL_SET_DEFAULT_ACTION, 1));
    fw_os_err_t os_err = filter_err_to_os_err(seL4_GetMR(FILTER_RET_ERR));
    if (os_err != OS_ERR_OKAY) {
        sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[os_err]);
        mp_raise_OSError(os_err);
        return mp_obj_new_int_from_uint(os_err);
    }

    webserver_state.filter_states[protocol_match].default_action = action;
    return mp_obj_new_int_from_uint(os_err);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_3(filter_set_default_action_obj,
                                 filter_set_default_action);

/* Get interface filter default action */
STATIC mp_obj_t filter_get_default_action(mp_obj_t interface_idx_in,
                                          mp_obj_t protocol_in) {
    uint8_t interface_idx = mp_obj_get_int(interface_idx_in);
    if (interface_idx >= FIREWALL_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    uint16_t protocol = mp_obj_get_int(protocol_in);
    for (uint8_t i = 0; i < firewall_config.num_filters; i++) {
        if (firewall_config.filters[i].protocol == protocol &&
            firewall_config.filter_iface_id[i] == interface_idx) {
        return mp_obj_new_int_from_uint(
            webserver_state.filter_states[i].default_action);
        }
    }

    sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[OS_ERR_INVALID_PROTOCOL]);
    mp_raise_OSError(OS_ERR_INVALID_PROTOCOL);
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_2(filter_get_default_action_obj,
                                 filter_get_default_action);

/* Get the nth interface filter rule */
STATIC mp_obj_t rule_get_nth(mp_obj_t interface_idx_in, mp_obj_t protocol_in,
                             mp_obj_t rule_idx_in) {
    uint8_t interface_idx = mp_obj_get_int(interface_idx_in);
    if (interface_idx >= FIREWALL_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    uint16_t protocol = mp_obj_get_int(protocol_in);
    uint16_t rule_idx = mp_obj_get_int(rule_idx_in);
    uint8_t protocol_match = firewall_config.num_filters;
    for (uint8_t i = 0; i < firewall_config.num_filters; i++) {
        if (firewall_config.filters[i].protocol == protocol &&
            firewall_config.filter_iface_id[i] == interface_idx) {
        protocol_match = i;
        break;
        }
    }

    if (protocol_match == firewall_config.num_filters) {
        sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[OS_ERR_INVALID_PROTOCOL]);
        mp_raise_OSError(OS_ERR_INVALID_PROTOCOL);
        return mp_const_none;
    }

    if (rule_idx >= webserver_state.num_rules[protocol_match] ||
        rule_idx >= firewall_config.rules_capacity) {
        sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[OS_ERR_INVALID_RULE_NUM]);
        mp_raise_OSError(OS_ERR_INVALID_RULE_NUM);
        return mp_const_none;
    }

    uint16_t valid_rules = 0;
    for (uint16_t i = 0; i < webserver_state.filter_states[protocol_match].rules_capacity; i++) {
            firewall_rule_t *rule = (firewall_rule_t *)(webserver_state.filter_states[protocol_match].rules + i);
            if (!rule->valid) {
                continue;
            }

            if (valid_rules == rule_idx) {
                mp_obj_t tuple[10];
                tuple[0] = mp_obj_new_int_from_uint(i);
                tuple[1] = mp_obj_new_int_from_uint(rule->src_ip);
                tuple[2] = mp_obj_new_int_from_uint(rule->src_port);
                tuple[3] = mp_obj_new_int_from_uint(rule->src_port_any);
                tuple[4] = mp_obj_new_int_from_uint(rule->dst_ip);
                tuple[5] = mp_obj_new_int_from_uint(rule->dst_port);
                tuple[6] = mp_obj_new_int_from_uint(rule->dst_port_any);
                tuple[7] = mp_obj_new_int_from_uint(rule->src_subnet);
                tuple[8] = mp_obj_new_int_from_uint(rule->dst_subnet);
                tuple[9] = mp_obj_new_int_from_uint(rule->action);
                return mp_obj_new_tuple(10, tuple);
            }

        valid_rules++;
    }

    sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[OS_ERR_INTERNAL_ERROR]);
    mp_raise_OSError(OS_ERR_INTERNAL_ERROR);
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_3(rule_get_nth_obj, rule_get_nth);

STATIC const mp_rom_map_elem_t lions_firewall_module_globals_table[] = {
    {MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_lions_firewall)},
    {MP_ROM_QSTR(MP_QSTR_interface_mac_get),
     MP_ROM_PTR(&interface_get_mac_obj)},
    {MP_ROM_QSTR(MP_QSTR_interface_ip_get), MP_ROM_PTR(&interface_get_ip_obj)},
    {MP_ROM_QSTR(MP_QSTR_route_add), MP_ROM_PTR(&route_add_obj)},
    {MP_ROM_QSTR(MP_QSTR_route_delete), MP_ROM_PTR(&route_delete_obj)},
    {MP_ROM_QSTR(MP_QSTR_route_count), MP_ROM_PTR(&route_count_obj)},
    {MP_ROM_QSTR(MP_QSTR_route_get_nth), MP_ROM_PTR(&route_get_nth_obj)},
    {MP_ROM_QSTR(MP_QSTR_rule_add), MP_ROM_PTR(&rule_add_obj)},
    {MP_ROM_QSTR(MP_QSTR_rule_delete), MP_ROM_PTR(&rule_delete_obj)},
    {MP_ROM_QSTR(MP_QSTR_rule_count), MP_ROM_PTR(&rule_count_obj)},
    {MP_ROM_QSTR(MP_QSTR_rule_get_nth), MP_ROM_PTR(&rule_get_nth_obj)},
    {MP_ROM_QSTR(MP_QSTR_filter_get_default_action),
     MP_ROM_PTR(&filter_get_default_action_obj)},
    {MP_ROM_QSTR(MP_QSTR_filter_set_default_action),
     MP_ROM_PTR(&filter_set_default_action_obj)},
};

STATIC MP_DEFINE_CONST_DICT(lions_firewall_module_globals,
                            lions_firewall_module_globals_table);

const mp_obj_module_t lions_firewall_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&lions_firewall_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_lions_firewall, lions_firewall_module);
