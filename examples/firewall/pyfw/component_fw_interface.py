# Copyright 2026, UNSW SPDX-License-Identifier: BSD-2-Clause

from typing import Dict, Iterator, Optional
from sdfgen import SystemDescription
from pyfw.component_arp import ArpRequester, ArpResponder
from pyfw.component_base import Component
from pyfw.component_filter import Filter
from pyfw.component_net_interface import NetworkInterface
from pyfw.component_net_virt import NetVirtRx, NetVirtTx
from pyfw.constants import BuildConstants
from pyfw.specs import FirewallMemoryRegion, TrackedNet

class FirewallInterface(NetworkInterface):
    def __init__(self,
                 network_interface: NetworkInterface,
    ) -> None:
        super().__init__(index=network_interface.index,
                         name=network_interface.name,
                         board_ethernet=network_interface.board_ethernet,
                         mac=network_interface.mac,
                         ip=network_interface.ip,
                         subnet_bits=network_interface.subnet_bits,
                         priorities=network_interface.priorities)

        self._ethernet_driver: Optional[SystemDescription.ProtectionDomain] = None
        self._rx_virtualiser: Optional[NetVirtRx] = None
        self._tx_virtualiser: Optional[NetVirtTx] = None
        self._arp_requester: Optional[ArpRequester] = None
        self._arp_responder: Optional[ArpResponder] = None
        self._filters: Dict[int, Filter] = dict()
        self._rx_dma_region: Optional[FirewallMemoryRegion] = None
        self._net_system: Optional[TrackedNet] = None

    @property
    def ethernet_driver(self) -> SystemDescription.ProtectionDomain:
        assert self._ethernet_driver is not None
        return self._ethernet_driver


    @ethernet_driver.setter
    def ethernet_driver(self, ethernet_driver: SystemDescription.ProtectionDomain):
        assert ethernet_driver is not None
        self._ethernet_driver = ethernet_driver

    @property
    def rx_virtualiser(self) -> NetVirtRx:
        assert self._rx_virtualiser is not None
        return self._rx_virtualiser

    @rx_virtualiser.setter
    def rx_virtualiser(self, rx_virtualiser: NetVirtRx):
        assert rx_virtualiser is not None
        self._rx_virtualiser = rx_virtualiser

    @property
    def tx_virtualiser(self) -> NetVirtTx:
        assert self._tx_virtualiser is not None
        return self._tx_virtualiser

    @tx_virtualiser.setter
    def tx_virtualiser(self, tx_virtualiser: NetVirtTx):
        assert tx_virtualiser is not None
        self._tx_virtualiser = tx_virtualiser

    @property
    def arp_requester(self) -> ArpRequester:
        assert self._arp_requester is not None
        return self._arp_requester

    @arp_requester.setter
    def arp_requester(self, arp_requester: ArpRequester):
        assert arp_requester is not None
        self._arp_requester = arp_requester

    @property
    def arp_responder(self) -> ArpResponder:
        assert self._arp_responder is not None
        return self._arp_responder

    @arp_responder.setter
    def arp_responder(self, arp_responder: ArpResponder):
        assert arp_responder is not None
        self._arp_responder = arp_responder

    @property
    def rx_dma_region(self) -> FirewallMemoryRegion:
        assert self._rx_dma_region is not None
        return self._rx_dma_region

    @rx_dma_region.setter
    def rx_dma_region(self, rx_dma_region: FirewallMemoryRegion):
        assert rx_dma_region is not None
        self._rx_dma_region = rx_dma_region

    @property
    def net_system(self) -> TrackedNet:
        assert self._net_system is not None
        return self._net_system

    @net_system.setter
    def net_system(self, net_system: TrackedNet):
        assert net_system is not None
        self._net_system = net_system

    @property
    def out_dir(self) -> str:
        return f"{BuildConstants.output_dir()}/net_data{self.index}"

    def all_components(self) -> Iterator[Component]:
        yield self.rx_virtualiser
        yield self.tx_virtualiser
        yield self.arp_requester
        yield self.arp_responder
        for ip_filter in self.filters.values():
            yield ip_filter
