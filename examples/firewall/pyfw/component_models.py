# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from dataclasses import dataclass, field
from typing import Any, Dict, Iterator, List, Tuple

from sdfgen import SystemDescription

from pyfw.component_arp import ArpRequester, ArpResponder
from pyfw.component_base import Component
from pyfw.component_filter import Filter
from pyfw.component_net_virt import NetVirtRx, NetVirtTx
from pyfw.component_router import RouterInterface
from pyfw.specs import FirewallMemoryRegion


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
    index: int
    name: str
    ethernet_node_path: str
    mac: Tuple[int, ...]
    ip: str
    subnet_bits: int

    priorities: InterfacePriorities = field(default_factory=InterfacePriorities)

    # Populated during build in meta.py.
    driver: SystemDescription.ProtectionDomain = field(init=False)
    rx_virt: NetVirtRx = field(init=False)
    tx_virt: NetVirtTx = field(init=False)
    arp_requester: ArpRequester = field(init=False)
    arp_responder: ArpResponder = field(init=False)
    filters: Dict[int, Filter] = field(default_factory=dict)
    net_system: Any = field(init=False)
    rx_dma_region: FirewallMemoryRegion = field(init=False)
    router_interface: RouterInterface = field(init=False)

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

    def all_components(self) -> Iterator[Component]:
        yield self.rx_virt
        yield self.tx_virt
        yield self.arp_requester
        yield self.arp_responder
        for filt in self.filters.values():
            yield filt
