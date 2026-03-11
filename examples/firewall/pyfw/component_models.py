# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from typing import Dict, Iterator
from sdfgen import SystemDescription
from pyfw.component_arp import ArpRequester, ArpResponder
from pyfw.component_base import Component
from pyfw.component_filter import Filter
from pyfw.component_net_virt import NetVirtRx, NetVirtTx
from pyfw.component_router import RouterInterface
from pyfw.constants import NetworkInterface
from pyfw.specs import TrackedNet, FirewallMemoryRegion
import pyfw.constants

# TODO: Perhaps this should not be here?
class InterfacePriorities:
    ethernet_driver: int = 101
    tx_virtualiser: int = 100
    rx_virtualiser: int = 99
    arp_requester: int = 98
    arp_responder: int = 95
    filters: dict[str, int] = {
        "icmp": 90,
        "udp": 91,
        "tcp": 92,
    }

class FwNetworkInterface(NetworkInterface):
    def __init__(self,
                 network_interface: NetworkInterface,
                 ethernet_node_path: str,
                 priorities: InterfacePriorities
    ) -> None:
        super().__init__(network_interface.index,
                         network_interface.name,
                         network_interface.mac,
                         network_interface.ip,
                         network_interface.subnet_bits)

        # TODO: Probably want a better solution to this. I wanted to put it in constants.py, but it references Boards which is defined in meta.py
        self.ethernet_node_path = ethernet_node_path

        # TODO: Perhaps this should also be an optional argument set in constants.py
        self.priorities = priorities

        # Populated in meta.py.
        self.ethernet_driver: SystemDescription.ProtectionDomain = None
        self.rx_virtualiser: NetVirtRx = None
        self.tx_virtualiser: NetVirtTx = None
        self.arp_requester: ArpRequester = None
        self.arp_responder: ArpResponder = None
        self.filters: Dict[int, Filter] = dict()
        self.rx_dma_region: FirewallMemoryRegion = None
        self.net_system: TrackedNet = None

    @property
    def out_dir(self) -> str:
        return f"{pyfw.constants.output_dir}/net_data{self.index}"

    def all_components(self) -> Iterator[Component]:
        yield self.rx_virtualiser
        yield self.tx_virtualiser
        yield self.arp_requester
        yield self.arp_responder
        for ip_filter in self.filters.values():
            yield ip_filter
