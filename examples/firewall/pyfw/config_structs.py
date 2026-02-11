# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause
#
# CStruct definitions for firewall config structs.
# Python is the single source of truth -- generate_header() produces the C header file

import ctypes
from pyfw.cstruct import CStruct, CArray, generate_header

ALL_INCLUDES = [
    "#include <sddf/resources/common.h>",
    "#include <sddf/resources/device.h>",
    "#include <stdint.h>",
    "#include <stdbool.h>",
]

ETH_HWADDR_LEN = 6
SDDF_NET_MAX_CLIENTS = 64
FW_MAX_FW_CLIENTS = 61
FW_MAX_FILTERS = 61
FW_NUM_ARP_REQUESTER_CLIENTS = 2
FW_MAX_INTERFACES = 4
FW_MAX_INITIAL_FILTER_RULES = 4
FW_INTERFACE_NAME_LEN = 32
FW_DEBUG_OUTPUT = 1

ALL_CONSTANTS = {
    "ETH_HWADDR_LEN": ETH_HWADDR_LEN,
    "SDDF_NET_MAX_CLIENTS": SDDF_NET_MAX_CLIENTS,
    "FW_MAX_FW_CLIENTS": FW_MAX_FW_CLIENTS,
    "FW_MAX_FILTERS": FW_MAX_FILTERS,
    "FW_NUM_ARP_REQUESTER_CLIENTS": FW_NUM_ARP_REQUESTER_CLIENTS,
    "FW_MAX_INTERFACES": FW_MAX_INTERFACES,
    "FW_MAX_INITIAL_FILTER_RULES": FW_MAX_INITIAL_FILTER_RULES,
    "FW_INTERFACE_NAME_LEN": FW_INTERFACE_NAME_LEN,
    "FW_DEBUG_OUTPUT": FW_DEBUG_OUTPUT,
}

class RegionResource(CStruct):
    extern = True
    c_name = "region_resource"
    section_name = "region_resource"
    _fields_ = [
        ("vaddr", ctypes.c_uint64),
        ("size", ctypes.c_uint64),
    ]

    def __init__(self,*, vaddr=0, size=0):
        super().__init__()
        self.vaddr = vaddr
        self.size = size


class DeviceRegionResource(CStruct):
    extern = True
    c_name = "device_region_resource"
    section_name = "device_region_resource"
    _fields_ = [
        ("region", RegionResource),
        ("io_addr", ctypes.c_uint64),
    ]

    def __init__(self,*, region=None, io_addr=0):
        super().__init__()
        if region is not None:
            self.region = region
        self.io_addr = io_addr


class FwConnectionResource(CStruct):
    c_name = "fw_connection_resource"
    section_name = "fw_connection_resource"
    _fields_ = [
        ("queue", RegionResource),
        ("capacity", ctypes.c_uint16),
        ("ch", ctypes.c_uint8),
    ]

    def __init__(self,*, queue=None, capacity=0, ch=0):
        super().__init__()
        if queue is not None:
            self.queue = queue
        self.capacity = capacity
        self.ch = ch


class FwDataConnectionResource(CStruct):
    c_name = "fw_data_connection_resource"
    section_name = "fw_data_connection_resource"
    _fields_ = [
        ("conn", FwConnectionResource),
        ("data", DeviceRegionResource),
    ]

    def __init__(self,*, fw_conn=None, dev_conn=None):
        super().__init__()
        if fw_conn is not None:
            self.conn = fw_conn
        if dev_conn is not None:
            self.data = dev_conn


class FwArpConnection(CStruct):
    c_name = "fw_arp_connection"
    section_name = "fw_arp_connection"
    _fields_ = [
        ("request", RegionResource),
        ("response", RegionResource),
        ("capacity", ctypes.c_uint16),
        ("ch", ctypes.c_uint8),
    ]

    def __init__(self,*, request=None, response=None, capacity=0, ch=0):
        super().__init__()
        if request is not None:
            self.request = request
        if response is not None:
            self.response = response
        self.capacity = capacity
        self.ch = ch


class FwRule(CStruct):
    c_name = "fw_rule"
    section_name = "fw_rule"
    _fields_ = [
        ("action", ctypes.c_uint8),
        ("src_ip", ctypes.c_uint32),
        ("dst_ip", ctypes.c_uint32),
        ("src_port", ctypes.c_uint16),
        ("dst_port", ctypes.c_uint16),
        ("src_subnet", ctypes.c_uint8),
        ("dst_subnet", ctypes.c_uint8),
        ("src_port_any", ctypes.c_bool),
        ("dst_port_any", ctypes.c_bool),
        ("rule_id", ctypes.c_uint16),
    ]

    def __init__(self, *, action=0, src_ip=0, dst_ip=0, src_port=0, dst_port=0,
                 src_subnet=0, dst_subnet=0, src_port_any=False,
                 dst_port_any=False, rule_id=0):
        super().__init__()
        self.action = action
        self.src_ip = src_ip
        self.dst_ip = dst_ip
        self.src_port = src_port
        self.dst_port = dst_port
        self.src_subnet = src_subnet
        self.dst_subnet = dst_subnet
        self.src_port_any = src_port_any
        self.dst_port_any = dst_port_any
        self.rule_id = rule_id


class FwWebserverFilterConfig(CStruct):
    c_name = "fw_webserver_filter_config"
    section_name = "fw_webserver_filter_config"
    _fields_ = [
        ("protocol", ctypes.c_uint16),
        ("ch", ctypes.c_uint8),
        ("rules", RegionResource),
        ("rules_capacity", ctypes.c_uint16),
    ]

    def __init__(self, *, protocol=0, ch=0, rules=None, rules_capacity=0):
        super().__init__()
        self.protocol = protocol
        self.ch = ch
        if rules is not None:
            self.rules = rules
        self.rules_capacity = rules_capacity


class FwWebserverRouterConfig(CStruct):
    c_name = "fw_webserver_router_config"
    section_name = "fw_webserver_router_config"
    _fields_ = [
        ("routing_ch", ctypes.c_uint8),
        ("routing_table", RegionResource),
        ("routing_table_capacity", ctypes.c_uint16),
    ]

    def __init__(self, *, routing_ch=0, routing_table=None, routing_table_capacity=0):
        super().__init__()
        self.routing_ch = routing_ch
        if routing_table is not None:
            self.routing_table = routing_table
        self.routing_table_capacity = routing_table_capacity


class FwNetVirtRxConfig(CStruct):
    c_name = "fw_net_virt_rx_config"
    section_name = "fw_net_virt_rx_config"
    _fields_ = [
        ("interface", ctypes.c_uint8),
        ("active_client_ethtypes", CArray(ctypes.c_uint16, "SDDF_NET_MAX_CLIENTS", ALL_CONSTANTS)),
        ("active_client_subtypes", CArray(ctypes.c_uint16, "SDDF_NET_MAX_CLIENTS", ALL_CONSTANTS)),
        ("free_clients", CArray(FwConnectionResource, "FW_MAX_FW_CLIENTS", ALL_CONSTANTS)),
        ("num_free_clients", ctypes.c_uint8),
    ]

    def __init__(self, *, interface=0, active_client_ethtypes=None,
                 active_client_subtypes=None, free_clients=None):
        super().__init__()
        self.interface = interface
        if active_client_ethtypes:
            for i, v in enumerate(active_client_ethtypes):
                self.active_client_ethtypes[i] = v
        if active_client_subtypes:
            for i, v in enumerate(active_client_subtypes):
                self.active_client_subtypes[i] = v
        if free_clients:
            for i, v in enumerate(free_clients):
                self.free_clients[i] = v
            self.num_free_clients = len(free_clients)


class FwNetVirtTxConfig(CStruct):
    c_name = "fw_net_virt_tx_config"
    section_name = "fw_net_virt_tx_config"
    _fields_ = [
        ("interface", ctypes.c_uint8),
        ("active_clients", CArray(FwDataConnectionResource, "FW_MAX_FW_CLIENTS", ALL_CONSTANTS)),
        ("num_active_clients", ctypes.c_uint8),
        ("free_clients", CArray(FwDataConnectionResource, "FW_MAX_FW_CLIENTS", ALL_CONSTANTS)),
        ("num_free_clients", ctypes.c_uint8),
    ]

    def __init__(self, *, interface=0, active_clients=None, free_clients=None):
        super().__init__()
        self.interface = interface
        if active_clients:
            for i, v in enumerate(active_clients):
                self.active_clients[i] = v
            self.num_active_clients = len(active_clients)
        if free_clients:
            for i, v in enumerate(free_clients):
                self.free_clients[i] = v
            self.num_free_clients = len(free_clients)


class FwArpRequesterConfig(CStruct):
    c_name = "fw_arp_requester_config"
    section_name = "fw_arp_requester_config"
    _fields_ = [
        ("interface", ctypes.c_uint8),
        ("mac_addr", CArray(ctypes.c_uint8, "ETH_HWADDR_LEN", ALL_CONSTANTS)),
        ("ip", ctypes.c_uint32),
        ("arp_clients", CArray(FwArpConnection, "FW_NUM_ARP_REQUESTER_CLIENTS", ALL_CONSTANTS)),
        ("num_arp_clients", ctypes.c_uint8),
        ("arp_cache", RegionResource),
        ("arp_cache_capacity", ctypes.c_uint16),
    ]

    def __init__(self, *, interface=0, mac_addr=None, ip=0, arp_clients=None,
                 arp_cache=None, arp_cache_capacity=0):
        super().__init__()
        self.interface = interface
        if mac_addr:
            for i, v in enumerate(mac_addr):
                self.mac_addr[i] = v
        self.ip = ip
        if arp_clients:
            for i, v in enumerate(arp_clients):
                self.arp_clients[i] = v
            self.num_arp_clients = len(arp_clients)
        if arp_cache is not None:
            self.arp_cache = arp_cache
        self.arp_cache_capacity = arp_cache_capacity


class FwArpResponderConfig(CStruct):
    c_name = "fw_arp_responder_config"
    section_name = "fw_arp_responder_config"
    _fields_ = [
        ("interface", ctypes.c_uint8),
        ("mac_addr", CArray(ctypes.c_uint8, "ETH_HWADDR_LEN", ALL_CONSTANTS)),
        ("ip", ctypes.c_uint32),
    ]

    def __init__(self, *, interface=0, mac_addr=None, ip=0):
        super().__init__()
        self.interface = interface
        if mac_addr:
            for i, v in enumerate(mac_addr):
                self.mac_addr[i] = v
        self.ip = ip


class FwFilterConfig(CStruct):
    c_name = "fw_filter_config"
    section_name = "fw_filter_config"
    _fields_ = [
        ("interface", ctypes.c_uint8),
        ("default_rule", FwRule),
        ("initial_rules", CArray(FwRule, "FW_MAX_INITIAL_FILTER_RULES", ALL_CONSTANTS)),
        ("num_initial_rules", ctypes.c_uint8),
        ("instances_capacity", ctypes.c_uint16),
        ("router", FwConnectionResource),
        ("internal_instances", RegionResource),
        ("external_instances", RegionResource),
        ("rules", RegionResource),
        ("rules_capacity", ctypes.c_uint16),
        ("rule_id_bitmap", RegionResource),
    ]

    def __init__(self, *, interface=0, default_rule=None, initial_rules=None,
                 instances_capacity=0, router=None, internal_instances=None,
                 external_instances=None, rules=None, rules_capacity=0,
                 rule_id_bitmap=None):
        super().__init__()
        self.interface = interface
        if default_rule is not None:
            self.default_rule = default_rule
        if initial_rules:
            for i, v in enumerate(initial_rules):
                self.initial_rules[i] = v
            self.num_initial_rules = len(initial_rules)
        self.instances_capacity = instances_capacity
        if router is not None:
            self.router = router
        if internal_instances is not None:
            self.internal_instances = internal_instances
        if external_instances is not None:
            self.external_instances = external_instances
        if rules is not None:
            self.rules = rules
        self.rules_capacity = rules_capacity
        if rule_id_bitmap is not None:
            self.rule_id_bitmap = rule_id_bitmap


class FwRouterInterface(CStruct):
    c_name = "fw_router_interface"
    section_name = "fw_router_interface"
    _fields_ = [
        ("rx_free", FwConnectionResource),
        ("tx_active", CArray(FwConnectionResource, "FW_MAX_INTERFACES", ALL_CONSTANTS)),
        ("data", RegionResource),
        ("arp_queue", FwArpConnection),
        ("arp_cache", RegionResource),
        ("arp_cache_capacity", ctypes.c_uint16),
        ("filters", CArray(FwConnectionResource, "FW_MAX_FILTERS", ALL_CONSTANTS)),
        ("num_filters", ctypes.c_uint8),
        ("mac_addr", CArray(ctypes.c_uint8, "ETH_HWADDR_LEN", ALL_CONSTANTS)),
        ("ip", ctypes.c_uint32),
        ("subnet", ctypes.c_uint32),
    ]

    def __init__(self, *, rx_free=None, tx_active=None, data=None,
                 arp_queue=None, arp_cache=None, arp_cache_capacity=0,
                 filters=None, mac_addr=None, ip=0, subnet=0):
        super().__init__()
        if rx_free is not None:
            self.rx_free = rx_free
        if tx_active:
            for i, v in enumerate(tx_active):
                self.tx_active[i] = v
        if data is not None:
            self.data = data
        if arp_queue is not None:
            self.arp_queue = arp_queue
        if arp_cache is not None:
            self.arp_cache = arp_cache
        self.arp_cache_capacity = arp_cache_capacity
        if filters:
            for i, v in enumerate(filters):
                self.filters[i] = v
            self.num_filters = len(filters)
        if mac_addr:
            for i, v in enumerate(mac_addr):
                self.mac_addr[i] = v
        self.ip = ip
        self.subnet = subnet


class FwRouterConfig(CStruct):
    c_name = "fw_router_config"
    section_name = "fw_router_config"
    _fields_ = [
        ("num_interfaces", ctypes.c_uint8),
        ("webserver_interface", ctypes.c_uint8),
        ("interfaces", CArray(FwRouterInterface, "FW_MAX_INTERFACES", ALL_CONSTANTS)),
        ("packet_queue", RegionResource),
        ("packet_waiting_capacity", ctypes.c_uint16),
        ("webserver", FwWebserverRouterConfig),
        ("icmp_module", FwConnectionResource),
        ("webserver_rx", FwConnectionResource),
    ]

    def __init__(self, *, webserver_interface=0, interfaces=None,
                 packet_queue=None, packet_waiting_capacity=0,
                 webserver=None, icmp_module=None, webserver_rx=None):
        super().__init__()
        self.webserver_interface = webserver_interface
        if interfaces:
            for i, v in enumerate(interfaces):
                self.interfaces[i] = v
            self.num_interfaces = len(interfaces)
        if packet_queue is not None:
            self.packet_queue = packet_queue
        self.packet_waiting_capacity = packet_waiting_capacity
        if webserver is not None:
            self.webserver = webserver
        if icmp_module is not None:
            self.icmp_module = icmp_module
        if webserver_rx is not None:
            self.webserver_rx = webserver_rx


class FwIcmpModuleConfig(CStruct):
    c_name = "fw_icmp_module_config"
    section_name = "fw_icmp_module_config"
    _fields_ = [
        ("ips", CArray(ctypes.c_uint32, "FW_MAX_INTERFACES", ALL_CONSTANTS)),
        ("router", FwConnectionResource),
        ("num_interfaces", ctypes.c_uint8),
    ]

    def __init__(self, *, ips=None, router=None, num_interfaces=0):
        super().__init__()
        if ips:
            for i, v in enumerate(ips):
                self.ips[i] = v
        if router is not None:
            self.router = router
        self.num_interfaces = num_interfaces


class FwWebserverInterfaceConfig(CStruct):
    c_name = "fw_webserver_interface_config"
    section_name = "fw_webserver_interface_config"
    _fields_ = [
        ("mac_addr", CArray(ctypes.c_uint8, "ETH_HWADDR_LEN", ALL_CONSTANTS)),
        ("ip", ctypes.c_uint32),
        ("filters", CArray(FwWebserverFilterConfig, "FW_MAX_FILTERS", ALL_CONSTANTS)),
        ("num_filters", ctypes.c_uint8),
        ("name", CArray(ctypes.c_uint8, "FW_INTERFACE_NAME_LEN", ALL_CONSTANTS)),
    ]

    def __init__(self, *, mac_addr=None, ip=0, filters=None, name=None):
        super().__init__()
        if mac_addr:
            for i, v in enumerate(mac_addr):
                self.mac_addr[i] = v
        self.ip = ip
        if filters:
            for i, v in enumerate(filters):
                self.filters[i] = v
            self.num_filters = len(filters)
        if name:
            for i, v in enumerate(name):
                self.name[i] = v


class FwWebserverConfig(CStruct):
    c_name = "fw_webserver_config"
    section_name = "fw_webserver_config"
    _fields_ = [
        ("interface", ctypes.c_uint8),
        ("rx_active", FwConnectionResource),
        ("data", RegionResource),
        ("rx_free", FwConnectionResource),
        ("arp_queue", FwArpConnection),
        ("router", FwWebserverRouterConfig),
        ("interfaces", CArray(FwWebserverInterfaceConfig, "FW_MAX_INTERFACES", ALL_CONSTANTS)),
        ("num_interfaces", ctypes.c_uint8),
    ]

    def __init__(self, *, interface=0, rx_active=None, data=None, rx_free=None,
                 arp_queue=None, router=None, interfaces=None):
        super().__init__()
        self.interface = interface
        if rx_active is not None:
            self.rx_active = rx_active
        if data is not None:
            self.data = data
        if rx_free is not None:
            self.rx_free = rx_free
        if arp_queue is not None:
            self.arp_queue = arp_queue
        if router is not None:
            self.router = router
        if interfaces:
            for i, v in enumerate(interfaces):
                self.interfaces[i] = v
            self.num_interfaces = len(interfaces)


# ---------------------------------------------------------------------------
# All config structs for header generation
# ---------------------------------------------------------------------------

ALL_STRUCTS = [
    FwConnectionResource,
    FwDataConnectionResource,
    FwArpConnection,
    FwRule,
    FwWebserverFilterConfig,
    FwWebserverRouterConfig,
    FwNetVirtRxConfig,
    FwNetVirtTxConfig,
    FwArpRequesterConfig,
    FwArpResponderConfig,
    FwFilterConfig,
    FwRouterInterface,
    FwRouterConfig,
    FwIcmpModuleConfig,
    FwWebserverInterfaceConfig,
    FwWebserverConfig,
]

if __name__ == "__main__":
    print(generate_header(*ALL_STRUCTS, constants=ALL_CONSTANTS, includes=ALL_INCLUDES))
