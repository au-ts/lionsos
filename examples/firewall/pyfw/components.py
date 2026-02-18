# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause
#
# Component classes for the firewall system.
# Each class encapsulates one process (PD) and its config struct.

from dataclasses import dataclass, field
from typing import List, Optional, Dict, Any, Tuple
from sdfgen import SystemDescription, Sddf, DeviceTree
from pyfw.config_structs import (
    RegionResource,
    DeviceRegionResource,
    FwConnectionResource,
    FwDataConnectionResource,
    FwArpConnection,
    FwNetVirtRxConfig,
    FwNetVirtTxConfig,
    FwArpRequesterConfig,
    FwArpResponderConfig,
    FwFilterConfig,
    FwRouterInterface,
    FwRouterConfig,
    FwIcmpModuleConfig,
    FwWebserverFilterConfig,
    FwWebserverRouterConfig,
    FwWebserverInterfaceConfig,
    FwWebserverConfig,
    FW_MAX_INTERFACES,
)
from pyfw.specs import ReMappableRegion, TopologyEdge

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

class Component:
    """Base class for all system components (processes)."""

    def __init__(self, name, elf, sdf, priority, budget=0, period=0, stack_size=0):
        self.sdf = sdf
        self.pd = ProtectionDomain(
            name,
            elf,
            priority=priority,
            budget=budget or None,
            period=period or None,
            stack_size=stack_size or None,
        )
        self._connections = {}
        self._regions = {}

    @property
    def name(self):
        return self.pd.name

    def register(self):
        self.sdf.add_pd(self.pd)

    def topology_connections(self):
        return self._connections

    def topology_regions(self):
        return self._regions


class NetVirtRx(Component):
    """Per-interface RX virtualiser."""

    def __init__(self, iface_index, sdf, priority):
        super().__init__(
            f"net_virt_rx{iface_index}",
            f"firewall_network_virt_rx{iface_index}.elf",
            sdf,
            priority,
        )
        self.iface_index = iface_index
        self._ethtypes = []
        self._subtypes = []
        self._free_clients = []

    def add_active_client(self, ethtype, subtype):
        self._ethtypes.append(ethtype)
        self._subtypes.append(subtype)

    def add_free_client(self, resource):
        self._free_clients.append(resource)

    def finalize_config(self):
        self.config = FwNetVirtRxConfig(
            interface=self.iface_index,
            active_client_ethtypes=self._ethtypes,
            active_client_subtypes=self._subtypes,
            free_clients=self._free_clients,
        )
        return self.config


class NetVirtTx(Component):
    """Per-interface TX virtualiser."""

    def __init__(self, iface_index, sdf, priority, budget=20000):
        super().__init__(
            f"net_virt_tx{iface_index}",
            f"firewall_network_virt_tx{iface_index}.elf",
            sdf,
            priority,
            budget=budget,
        )
        self.iface_index = iface_index
        self._active_clients = []
        self._free_clients = []

    def add_active_client(self, resource):
        self._active_clients.append(resource)

    def add_free_client(self, resource):
        self._free_clients.append(resource)

    def finalize_config(self):
        self.config = FwNetVirtTxConfig(
            interface=self.iface_index,
            active_clients=self._active_clients,
            free_clients=self._free_clients,
        )
        return self.config


class ArpRequester(Component):
    """Per-interface ARP requester."""

    def __init__(self, iface_index, sdf, mac, ip, priority, budget=20000):
        super().__init__(
            f"arp_requester{iface_index}",
            f"arp_requester{iface_index}.elf",
            sdf,
            priority,
            budget=budget,
        )
        self.iface_index = iface_index
        self.mac = mac
        self.ip = ip
        self._arp_clients = []
        self._arp_cache = None
        self._arp_cache_capacity = 0

    def add_arp_client(self, resource):
        self._arp_clients.append(resource)

    def set_cache(self, resource, capacity):
        self._arp_cache = resource
        self._arp_cache_capacity = capacity

    def finalize_config(self):
        self.config = FwArpRequesterConfig(
            interface=self.iface_index,
            mac_addr=self.mac,
            ip=self.ip,
            arp_clients=self._arp_clients,
            arp_cache=self._arp_cache,
            arp_cache_capacity=self._arp_cache_capacity,
        )
        return self.config


class ArpResponder(Component):
    """Per-interface ARP responder."""

    def __init__(self, iface_index, sdf, mac, ip, priority, budget=20000):
        super().__init__(
            f"arp_responder{iface_index}",
            f"arp_responder{iface_index}.elf",
            sdf,
            priority,
            budget=budget,
        )
        self.iface_index = iface_index
        self.mac = mac
        self.ip = ip

    def finalize_config(self):
        self.config = FwArpResponderConfig(
            interface=self.iface_index,
            mac_addr=self.mac,
            ip=self.ip,
        )
        return self.config


class Filter(Component):
    """Per-interface protocol filter (ICMP/TCP/UDP)."""

    PROTOCOL_NAMES = {0x01: "icmp", 0x06: "tcp", 0x11: "udp"}

    def __init__(self, iface_index, protocol, sdf, priority, budget=20000):
        proto_name = self.PROTOCOL_NAMES.get(protocol, f"proto{protocol}")
        super().__init__(
            f"{proto_name}_filter{iface_index}",
            f"{proto_name}_filter{iface_index}.elf",
            sdf,
            priority,
            budget=budget,
        )
        self.iface_index = iface_index
        self.protocol = protocol
        self._router_conn = None
        self._rules = None
        self._rules_capacity = 0
        self._rule_bitmap = None
        self._internal_instances = None
        self._external_instances = None
        self._instances_capacity = 0

    def set_router_connection(self, resource):
        self._router_conn = resource

    def set_rules_region(self, resource, capacity):
        self._rules = resource
        self._rules_capacity = capacity

    def set_rule_bitmap(self, resource):
        self._rule_bitmap = resource

    def set_instances(self, internal, external, capacity):
        self._internal_instances = internal
        self._external_instances = external
        self._instances_capacity = capacity

    def finalize_config(self):
        self.config = FwFilterConfig(
            interface=self.iface_index,
            instances_capacity=self._instances_capacity,
            router=self._router_conn,
            internal_instances=self._internal_instances,
            external_instances=self._external_instances,
            rules=self._rules,
            rules_capacity=self._rules_capacity,
            rule_id_bitmap=self._rule_bitmap,
        )
        return self.config



class RouterInterface:
    """Per-interface state held by the Router component."""

    def __init__(self):
        self.rx_free = None
        self.tx_active = [FwConnectionResource() for _ in range(FW_MAX_INTERFACES)]
        self.data = None
        self.arp_queue = None
        self.arp_cache = None
        self.arp_cache_capacity = 0
        self.filters = []
        self.mac_addr = None
        self.ip = 0
        self.subnet = 0

    def set_tx_active(self, dst_index, resource):
        self.tx_active[dst_index] = resource

    def add_filter(self, resource):
        self.filters.append(resource)

    def to_struct(self):
        return FwRouterInterface(
            rx_free=self.rx_free,
            tx_active=self.tx_active,
            data=self.data,
            arp_queue=self.arp_queue,
            arp_cache=self.arp_cache,
            arp_cache_capacity=self.arp_cache_capacity,
            filters=self.filters,
            mac_addr=self.mac_addr,
            ip=self.ip,
            subnet=self.subnet,
        )


class Router(Component):
    """Global router."""

    def __init__(self, sdf, priority=97, budget=20000):
        super().__init__("routing", "routing.elf", sdf, priority, budget=budget)
        self._interfaces = []
        self._packet_queue = None
        self._packet_waiting_capacity = 0
        self._webserver_config = None
        self._icmp_conn = None
        self._webserver_rx_conn = None
        self._webserver_interface_idx = 0

    def create_interface(self):
        ri = RouterInterface()
        self._interfaces.append(ri)
        return ri

    def set_packet_queue(self, resource, capacity):
        self._packet_queue = resource
        self._packet_waiting_capacity = capacity

    def set_webserver_config(self, config):
        self._webserver_config = config

    def set_webserver_interface(self, idx):
        self._webserver_interface_idx = idx

    def set_icmp_connection(self, resource):
        self._icmp_conn = resource

    def set_webserver_rx(self, resource):
        self._webserver_rx_conn = resource

    def finalize_config(self):
        self.config = FwRouterConfig(
            webserver_interface=self._webserver_interface_idx,
            interfaces=[ri.to_struct() for ri in self._interfaces],
            packet_queue=self._packet_queue,
            packet_waiting_capacity=self._packet_waiting_capacity,
            webserver=self._webserver_config,
            icmp_module=self._icmp_conn,
            webserver_rx=self._webserver_rx_conn,
        )
        return self.config


class Webserver(Component):
    """Global webserver."""

    def __init__(self, sdf, priority=1, budget=20000, stack_size=0x10000):
        super().__init__(
            "micropython",
            "micropython.elf",
            sdf,
            priority,
            budget=budget,
            stack_size=stack_size,
        )
        self._rx_active = None
        self._data = None
        self._rx_free = None
        self._arp_conn = None
        self._router_config = None
        self._interface_configs = []

    def set_rx_active(self, resource):
        self._rx_active = resource

    def set_data(self, resource):
        self._data = resource

    def set_rx_free(self, resource):
        self._rx_free = resource

    def set_arp_connection(self, resource):
        self._arp_conn = resource

    def set_router_config(self, config):
        self._router_config = config

    def add_interface_config(self, config):
        self._interface_configs.append(config)

    def set_interface(self, idx):
        self._interface_idx = idx

    def finalize_config(self):
        self.config = FwWebserverConfig(
            interface=self._interface_idx,
            rx_active=self._rx_active,
            data=self._data,
            rx_free=self._rx_free,
            arp_queue=self._arp_conn,
            router=self._router_config,
            interfaces=self._interface_configs,
        )
        return self.config


class IcmpModule(Component):
    """Global ICMP echo handler."""

    def __init__(self, sdf, priority=100, budget=20000):
        super().__init__(
            "icmp_module", "icmp_module.elf", sdf, priority, budget=budget
        )
        self._ips = []
        self._router_conn = None

    def add_ip(self, ip):
        self._ips.append(ip)

    def set_router_connection(self, resource):
        self._router_conn = resource

    def finalize_config(self):
        self.config = FwIcmpModuleConfig(
            ips=self._ips,
            router=self._router_conn,
            num_interfaces=len(self._ips),
        )
        return self.config


@dataclass
class InterfacePriorities:
    driver: int = 101
    tx_virt: int = 100
    rx_virt: int = 99
    arp_requester: int = 98
    arp_responder: int = 95
    icmp_filter: int = 90
    udp_filter: int = 91
    tcp_filter: int = 92


@dataclass
class NetworkInterface:
    """Definition of a network interface."""

    index: int
    name: str
    ethernet_node_path: str
    mac: Tuple[int, ...]
    ip: str
    subnet_bits: int

    priorities: InterfacePriorities = field(default_factory=InterfacePriorities)

    # Component references (populated during build)
    driver: Optional[ProtectionDomain] = None
    rx_virt: Optional[NetVirtRx] = None
    tx_virt: Optional[NetVirtTx] = None
    arp_requester: Optional[ArpRequester] = None
    arp_responder: Optional[ArpResponder] = None
    filters: Dict[int, Filter] = field(default_factory=dict)
    net_system: Optional[Any] = None
    rx_dma_region: Optional[ReMappableRegion] = None
    router_interface: Optional[RouterInterface] = None

    @property
    def out_dir(self) -> str:
        return f"net_data{self.index}"

    @property
    def ip_int(self) -> int:
        import ipaddress

        ip_split = self.ip.split(".")
        ip_split.reverse()
        reversed_ip = ".".join(ip_split)
        return int(ipaddress.IPv4Address(reversed_ip))

    @property
    def mac_list(self) -> List[int]:
        return list(self.mac)

    def all_components(self):
        """Yield all component objects for this interface."""
        if self.rx_virt:
            yield self.rx_virt
        if self.tx_virt:
            yield self.tx_virt
        if self.arp_requester:
            yield self.arp_requester
        if self.arp_responder:
            yield self.arp_responder
        for f in self.filters.values():
            yield f

    def all_pds(self) -> List[Tuple[ProtectionDomain, str]]:
        """Return all (pd, role_label) pairs for graph node rendering."""
        pds = []
        if self.driver:
            pds.append((self.driver, "Driver"))
        if self.rx_virt:
            pds.append((self.rx_virt.pd, "RX Virt"))
        if self.tx_virt:
            pds.append((self.tx_virt.pd, "TX Virt"))
        if self.arp_requester:
            pds.append((self.arp_requester.pd, "ARP Req"))
        if self.arp_responder:
            pds.append((self.arp_responder.pd, "ARP Resp"))
        proto_names = {0x01: "ICMP", 0x06: "TCP", 0x11: "UDP"}
        for proto, filt in self.filters.items():
            pds.append((filt.pd, f"{proto_names.get(proto, hex(proto))} Filter"))
        return pds
