# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from typing import List, Optional

from sdfgen import SystemDescription

from pyfw.component_base import Component
from pyfw.config_structs import (
    FwArpConnection,
    FwArpRequesterConfig,
    FwArpResponderConfig,
    RegionResource,
)


class ArpRequester(Component):
    def __init__(
        self,
        iface_index: int,
        sdf: SystemDescription,
        mac: List[int],
        ip: int,
        priority: int,
        budget: int = 20000,
    ) -> None:
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
        self._arp_clients: List[FwArpConnection] = []
        self._arp_cache: Optional[RegionResource] = None
        self._arp_cache_capacity = 0

    def add_arp_client(
        self,
        request: RegionResource,
        response: RegionResource,
        capacity: int,
        ch: int,
    ) -> None:
        self._arp_clients.append(
            FwArpConnection(request=request, response=response, capacity=capacity, ch=ch)
        )

    def set_cache(self, resource: RegionResource, capacity: int) -> None:
        self._arp_cache = resource
        self._arp_cache_capacity = capacity

    def finalize_config(self) -> FwArpRequesterConfig:
        assert self._arp_cache is not None
        assert len(self._arp_clients) > 0
        assert self._arp_cache_capacity > 0
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
    def __init__(
        self,
        iface_index: int,
        sdf: SystemDescription,
        mac: List[int],
        ip: int,
        priority: int,
        budget: int = 20000,
    ) -> None:
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

    def finalize_config(self) -> FwArpResponderConfig:
        assert len(self.mac) != 0
        assert self.ip != 0
        self.config = FwArpResponderConfig(
            interface=self.iface_index,
            mac_addr=self.mac,
            ip=self.ip,
        )
        return self.config
