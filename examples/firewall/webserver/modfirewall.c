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
    OS_ERR_INVALID_ROUTE_ARGS,/* Invalid arguments to add route */
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
    "Invalid arguments supplied to add route.",
    "Route or rule supplied already exists.",
    "Route or rule supplied clashes with an existing route or rule.",
    "Too many or too few arguments supplied.",
    "Route number supplied is greater than the number of routes.",
    "Rule number supplied is greater than the number of rules.",
    "Internal data structures are already at capacity.", // TODO we can check
    "Unknown internal error."
};

/* Convert a routing error to OS error */
fw_os_err_t fw_routing_err_to_os_err(fw_routing_err_t routing_err) {
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
        case ROUTING_ERR_INVALID_ROUTE:
            return OS_ERR_INVALID_ROUTE_ARGS;
        default:
            return OS_ERR_INTERNAL_ERROR;
    }
}

/* Convert a filter error to OS error */
fw_os_err_t filter_err_to_os_err(fw_filter_err_t filter_err) {
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

extern fw_webserver_config_t fw_config;

typedef struct fw_webserver_interface_state {
    fw_routing_table_t routing_table;
    uint16_t num_routes;
  
    fw_filter_state_t filter_states[FW_MAX_FILTERS];
    uint16_t num_rules[FW_MAX_FILTERS];
} fw_webserver_interface_state_t;

typedef struct fw_webserver_state {
    uint32_t ip;
    uint8_t mac_addr[ETH_HWADDR_LEN];

    fw_webserver_interface_state_t interfaces[FW_NUM_INTERFACES];
} fw_webserver_state_t;

fw_webserver_state_t webserver_state;

void fw_webserver_init(void) {
    webserver_state.ip = fw_config.interfaces[fw_config.interface].ip;
    sddf_memcpy(webserver_state.mac_addr, fw_config.interfaces[fw_config.interface].mac_addr, ETH_HWADDR_LEN);
    
    for (uint8_t i = 0; i < FW_NUM_INTERFACES; i++) {
        webserver_state.interfaces[i].routing_table.entries = fw_config.interfaces[i].router.routing_table.vaddr;
        webserver_state.interfaces[i].routing_table.capacity = fw_config.interfaces[i].router.routing_table_capacity;
        /* Hardcode pre-existing interface route. */
        webserver_state.interfaces[i].num_routes = 1;

        for (uint8_t j = 0; j < fw_config.interfaces[i].num_filters; j++) {
            fw_filter_state_init(&webserver_state.interfaces[i].filter_states[j],
                                    fw_config.interfaces[i].filters[j].rules.vaddr,
                                    fw_config.interfaces[i].filters[j].rules_capacity, 0, 0, 0,
                                    fw_config.interfaces[i].filters[j].default_action);
        }
    }

  /* Currently harcode pre-existing internal route to webserver. */
  webserver_state.interfaces[FW_INTERNAL_INTERFACE_ID].num_routes += 1;
}

/* Get MAC address for network interface */
STATIC mp_obj_t interface_get_mac(mp_obj_t interface_idx_in) {
    uint8_t interface_idx = mp_obj_get_int(interface_idx_in);
    if (interface_idx >= FW_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    mp_obj_t tuple[ETH_HWADDR_LEN];
    for (uint8_t i = 0; i < ETH_HWADDR_LEN; i++) {
        tuple[i] = mp_obj_new_int_from_uint(fw_config.interfaces[interface_idx].mac_addr[i]);
    }

    return mp_obj_new_tuple(ETH_HWADDR_LEN, tuple);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(interface_get_mac_obj, interface_get_mac);

/* Get IP address for network interface */
STATIC mp_obj_t interface_get_ip(mp_obj_t interface_idx_in) {
    uint8_t interface_idx = mp_obj_get_int(interface_idx_in);
    if (interface_idx >= FW_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    return mp_obj_new_int_from_uint(fw_config.interfaces[interface_idx].ip);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(interface_get_ip_obj, interface_get_ip);

/* Add a route to the routing table for a network interface */
STATIC mp_obj_t route_add(mp_uint_t n_args, const mp_obj_t *args) {
    if (n_args != 4) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_ARGUMENTS]);
        mp_raise_OSError(OS_ERR_INVALID_ARGUMENTS);
        return mp_const_none;
    }

    uint8_t interface_idx = mp_obj_get_int(args[0]);
    if (interface_idx >= FW_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    uint32_t ip = mp_obj_get_int(args[1]);
    uint8_t subnet = mp_obj_get_int(args[2]);
    uint32_t next_hop = mp_obj_get_int(args[3]);

    seL4_SetMR(ROUTER_ARG_IP, ip);
    seL4_SetMR(ROUTER_ARG_SUBNET, subnet);
    seL4_SetMR(ROUTER_ARG_NEXT_HOP, next_hop);

    microkit_msginfo msginfo =
        microkit_ppcall(fw_config.interfaces[interface_idx].router.routing_ch,
                        microkit_msginfo_new(FW_ADD_ROUTE, 4));
    fw_os_err_t os_err = fw_routing_err_to_os_err(seL4_GetMR(ROUTER_RET_ERR));
    if (os_err != OS_ERR_OKAY) {
        sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[os_err]);
        mp_raise_OSError(os_err);
        return mp_obj_new_int_from_uint(os_err);
    }

    webserver_state.interfaces[interface_idx].num_routes += 1;
    return mp_obj_new_int_from_uint(os_err);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_VAR(route_add_obj, 4, route_add);

/* Delete a route from the interface routing table */
STATIC mp_obj_t route_delete(mp_obj_t interface_idx_in, mp_obj_t route_id_in) {
    uint8_t interface_idx =   mp_obj_get_int(interface_idx_in);
    if (interface_idx >= FW_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    uint16_t route_id = mp_obj_get_int(route_id_in);

    seL4_SetMR(ROUTER_ARG_ROUTE_ID, route_id);
    microkit_msginfo msginfo =
        microkit_ppcall(fw_config.interfaces[interface_idx].router.routing_ch,
                        microkit_msginfo_new(FW_DEL_ROUTE, 1));
    fw_os_err_t os_err = fw_routing_err_to_os_err(seL4_GetMR(ROUTER_RET_ERR));
    if (os_err != OS_ERR_OKAY) {
        sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[os_err]);
        mp_raise_OSError(os_err);
        return mp_obj_new_int_from_uint(os_err);
    }

    webserver_state.interfaces[interface_idx].num_routes -= 1;
    return mp_obj_new_int_from_uint(route_id);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_2(route_delete_obj, route_delete);

/* Count the number of routes in an interface routing table */
STATIC mp_obj_t route_count(mp_obj_t interface_idx_in) {
    uint8_t interface_idx =   mp_obj_get_int(interface_idx_in);
    if (interface_idx >= FW_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    return mp_obj_new_int_from_uint(webserver_state.interfaces[interface_idx].num_routes);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(route_count_obj, route_count);

/* Return nth route in interface routing table */
STATIC mp_obj_t route_get_nth(mp_obj_t interface_idx_in,
                              mp_obj_t route_idx_in) {
    uint8_t interface_idx =   mp_obj_get_int(interface_idx_in);
    if (interface_idx >= FW_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    uint16_t route_idx = mp_obj_get_int(route_idx_in);
    if (route_idx >= webserver_state.interfaces[interface_idx].num_routes ||
        route_idx >= webserver_state.interfaces[interface_idx].routing_table.capacity) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_ROUTE_NUM]);
        mp_raise_OSError(OS_ERR_INVALID_ROUTE_NUM);
        return mp_const_none;
    }

    uint16_t valid_entries = 0;
    for (uint16_t i = 0;
        i < webserver_state.interfaces[interface_idx].routing_table.size; i++) {
        fw_routing_entry_t *entry =
            (fw_routing_entry_t
                *)(webserver_state.interfaces[interface_idx].routing_table.entries + i);

        if (valid_entries == route_idx) {
            mp_obj_t tuple[4];
            tuple[0] = mp_obj_new_int_from_uint(i);
            tuple[1] = mp_obj_new_int_from_uint(entry->ip);
            tuple[2] = mp_obj_new_int_from_uint(entry->subnet);
            tuple[3] = mp_obj_new_int_from_uint(entry->next_hop);
            return mp_obj_new_tuple(4, tuple);
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
    if (interface_idx >= FW_NUM_INTERFACES) {
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

    uint8_t protocol_match = fw_config.interfaces[interface_idx].num_filters;
    for (uint8_t i = 0; i < fw_config.interfaces[interface_idx].num_filters; i++) {
        if (fw_config.interfaces[interface_idx].filters[i].protocol == protocol) {
            protocol_match = i;
            break;
        }
    }

    if (protocol_match == fw_config.interfaces[interface_idx].num_filters) {
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
        microkit_ppcall(fw_config.interfaces[interface_idx].filters[protocol_match].ch,
                        microkit_msginfo_new(FW_ADD_RULE, 10));
    fw_os_err_t os_err = filter_err_to_os_err(seL4_GetMR(FILTER_RET_ERR));
    if (os_err != OS_ERR_OKAY) {
        sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[os_err]);
        mp_raise_OSError(os_err);
        return mp_obj_new_int_from_uint(os_err);
    }

    uint16_t rule_id = seL4_GetMR(FILTER_RET_RULE_ID);
    webserver_state.interfaces[interface_idx].num_rules[protocol_match] += 1;
    return mp_obj_new_int_from_uint(rule_id);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_VAR(rule_add_obj, 9, rule_add);

/* Delete a filter on an interface */
STATIC mp_obj_t rule_delete(mp_obj_t interface_idx_in, mp_obj_t rule_id_in,
                            mp_obj_t protocol_in) {
    uint8_t interface_idx = mp_obj_get_int(interface_idx_in);
    if (interface_idx >= FW_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    uint16_t rule_id = mp_obj_get_int(rule_id_in);
    uint16_t protocol = mp_obj_get_int(protocol_in);
    uint8_t protocol_match = fw_config.interfaces[interface_idx].num_filters;
    for (uint8_t i = 0; i < fw_config.interfaces[interface_idx].num_filters; i++) {
        if (fw_config.interfaces[interface_idx].filters[i].protocol == protocol) {
            protocol_match = i;
            break;
        }
    }

    if (protocol_match == fw_config.interfaces[interface_idx].num_filters) {
        sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[OS_ERR_INVALID_PROTOCOL]);
        mp_raise_OSError(OS_ERR_INVALID_PROTOCOL);
        return mp_const_none;
    }

    seL4_SetMR(FILTER_ARG_RULE_ID, rule_id);
    microkit_msginfo msginfo =
        microkit_ppcall(fw_config.interfaces[interface_idx].filters[protocol_match].ch,
                        microkit_msginfo_new(FW_DEL_RULE, 2));
    fw_os_err_t os_err = filter_err_to_os_err(seL4_GetMR(FILTER_RET_ERR));
    if (os_err != OS_ERR_OKAY) {
        sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[os_err]);
        mp_raise_OSError(os_err);
        return mp_obj_new_int_from_uint(os_err);
    }

    webserver_state.interfaces[interface_idx].num_rules[protocol_match] -= 1;
    return mp_obj_new_int_from_uint(rule_id);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_3(rule_delete_obj, rule_delete);

/* Get number of filter rules for a filter */
STATIC mp_obj_t rule_count(mp_obj_t interface_idx_in, mp_obj_t protocol_in) {
    uint8_t interface_idx = mp_obj_get_int(interface_idx_in);
    if (interface_idx >= FW_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    uint16_t protocol = mp_obj_get_int(protocol_in);
    for (uint8_t i = 0; i < fw_config.interfaces[interface_idx].num_filters; i++) {
        if (fw_config.interfaces[interface_idx].filters[i].protocol == protocol) {
            return mp_obj_new_int_from_uint(webserver_state.interfaces[interface_idx].num_rules[i]);
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
    if (interface_idx >= FW_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    uint16_t protocol = mp_obj_get_int(protocol_in);
    uint8_t action = mp_obj_get_int(action_in);
    uint8_t protocol_match = fw_config.interfaces[interface_idx].num_filters;
    for (uint8_t i = 0; i < fw_config.interfaces[interface_idx].num_filters; i++) {
        if (fw_config.interfaces[interface_idx].filters[i].protocol == protocol) {
            protocol_match = i;
            break;
        }
    }

    if (protocol_match == fw_config.interfaces[interface_idx].num_filters) {
        sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[OS_ERR_INVALID_PROTOCOL]);
        mp_raise_OSError(OS_ERR_INVALID_PROTOCOL);
        return mp_const_none;
    }

    seL4_SetMR(FILTER_ARG_ACTION, action);
    microkit_msginfo msginfo =
        microkit_ppcall(fw_config.interfaces[interface_idx].filters[protocol_match].ch,
                        microkit_msginfo_new(FW_SET_DEFAULT_ACTION, 1));
    fw_os_err_t os_err = filter_err_to_os_err(seL4_GetMR(FILTER_RET_ERR));
    if (os_err != OS_ERR_OKAY) {
        sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[os_err]);
        mp_raise_OSError(os_err);
        return mp_obj_new_int_from_uint(os_err);
    }

    webserver_state.interfaces[interface_idx].filter_states[protocol_match].default_action = action;
    return mp_obj_new_int_from_uint(os_err);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_3(filter_set_default_action_obj,
                                 filter_set_default_action);

/* Get interface filter default action */
STATIC mp_obj_t filter_get_default_action(mp_obj_t interface_idx_in,
                                          mp_obj_t protocol_in) {
    uint8_t interface_idx = mp_obj_get_int(interface_idx_in);
    if (interface_idx >= FW_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    uint16_t protocol = mp_obj_get_int(protocol_in);
    for (uint8_t i = 0; i < fw_config.interfaces[interface_idx].num_filters; i++) {
        if (fw_config.interfaces[interface_idx].filters[i].protocol == protocol) {
            return mp_obj_new_int_from_uint(
                webserver_state.interfaces[interface_idx].filter_states[i].default_action);
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
    if (interface_idx >= FW_NUM_INTERFACES) {
        sddf_dprintf("WEBSERVER|LOG: %s\n",
                    fw_os_err_str[OS_ERR_INVALID_INTERFACE]);
        mp_raise_OSError(OS_ERR_INVALID_INTERFACE);
        return mp_const_none;
    }

    uint16_t protocol = mp_obj_get_int(protocol_in);
    uint16_t rule_idx = mp_obj_get_int(rule_idx_in);
    uint8_t protocol_match = fw_config.interfaces[interface_idx].num_filters;
    for (uint8_t i = 0; i < fw_config.interfaces[interface_idx].num_filters; i++) {
        if (fw_config.interfaces[interface_idx].filters[i].protocol == protocol) {
            protocol_match = i;
            break;
        }
    }

    if (protocol_match == fw_config.interfaces[interface_idx].num_filters) {
        sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[OS_ERR_INVALID_PROTOCOL]);
        mp_raise_OSError(OS_ERR_INVALID_PROTOCOL);
        return mp_const_none;
    }

    if (rule_idx >= webserver_state.interfaces[interface_idx].num_rules[protocol_match] ||
        rule_idx >= fw_config.interfaces[interface_idx].filters[protocol_match].rules_capacity) {
        sddf_dprintf("WEBSERVER|LOG: %s\n", fw_os_err_str[OS_ERR_INVALID_RULE_NUM]);
        mp_raise_OSError(OS_ERR_INVALID_RULE_NUM);
        return mp_const_none;
    }

    uint16_t valid_rules = 0;
    for (uint16_t i = 0; i < webserver_state.interfaces[interface_idx].filter_states[protocol_match].rules_capacity; i++) {
            fw_rule_t *rule = (fw_rule_t *)(webserver_state.interfaces[interface_idx].filter_states[protocol_match].rules + i);
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
