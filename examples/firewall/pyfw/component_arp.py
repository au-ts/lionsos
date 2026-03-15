# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from sdfgen import SystemDescription
from pyfw.component_base import Component
from pyfw.config_structs import (
    EthHwaddrLen,
    FwArpConnection,
    FwMaxArpRequesterClients,
    FwArpRequesterConfig,
    FwArpResponderConfig,
    RegionResource,
)
from pyfw.constants import (
    NetworkInterface,
    arp_cache_buffer,
    arp_cache_region,
    arp_queue_buffer,
    arp_queue_region,
)
import pyfw.constants
from pyfw.specs import FirewallMemoryRegion

SDF_Channel = SystemDescription.Channel

class ArpRequester(Component, FwArpRequesterConfig):
    def __init__(
        self,
        net_interface: NetworkInterface,
        priority: int,
        budget: int = 20000,
    ) -> None:
        # Initialise base component class
        super().__init__(
            f"arp_requester{net_interface.index}",
            f"arp_requester{net_interface.index}.elf",
            priority,
            budget,
        )

        # Create an ARP entry cache
        self._arp_cache_mr = FirewallMemoryRegion(
            "arp_cache_" + self.name,
            arp_cache_region.region_size,
        )

        # Initialise ARP requester config class
        FwArpRequesterConfig.__init__(
            self,
            interface=net_interface.index,
            mac_addr=net_interface.mac_list,
            ip=net_interface.ip_int,
            arp_clients=[],
            arp_cache=self._arp_cache_mr.map(self.pd, "rw"),
            arp_cache_capacity=arp_cache_buffer.capacity,
        )


    # Add a client to the ARP requester - allows the client to make ARP requests
    # and receive responses. Returns client ARP connection.
    def add_arp_client(
        self,
        client: Component,
    ) -> FwArpConnection:

        # Create and map the ARP request queue memory region
        arp_req_mr = FirewallMemoryRegion(
            "fw_req_queue_" + client.name + "_" + self.name,
            arp_queue_region.region_size,
        )
        client_req_region = arp_req_mr.map(client.pd, "rw")
        arp_req_region = arp_req_mr.map(self.pd, "rw")

        # Create and map the ARP response queue memory region
        arp_res_mr = FirewallMemoryRegion(
            "fw_res_queue_" + client.name + "_" + self.name,
            arp_queue_region.region_size,
        )
        client_res_region = arp_res_mr.map(client.pd, "rw")
        arp_res_region = arp_res_mr.map(self.pd, "rw")

        ch = SDF_Channel(client.pd, self.pd)
        pyfw.constants.sdf.add_channel(ch)

        assert self.arp_clients is not None
        self.arp_clients.append(
            FwArpConnection(request=arp_req_region, response=arp_res_region,
                                   capacity=arp_queue_buffer.capacity, ch=ch.pd_b_id)
        )

        return FwArpConnection(request=client_req_region, response=client_res_region,
                               capacity=arp_queue_buffer.capacity, ch=ch.pd_a_id)

    # Maps the ARP entry cache read-only into another PD
    def share_cache(self, client: Component) -> RegionResource:
        client_cache_region = self._arp_cache_mr.map(client.pd, "r")
        return client_cache_region

    def finalise_config(self) -> None:
        assert self.mac_addr is not None
        assert len(self.mac_addr) == EthHwaddrLen
        assert self.ip is not None and self.ip != 0
        assert self.arp_clients is not None
        assert len(self.arp_clients) > 0
        assert len(self.arp_clients) <= FwMaxArpRequesterClients

class ArpResponder(Component, FwArpResponderConfig):
    def __init__(
        self,
        net_interface: NetworkInterface,
        priority: int,
        budget: int = 20000,
    ) -> None:
        # Initialise base component class
        super().__init__(
            f"arp_responder{net_interface.index}",
            f"arp_responder{net_interface.index}.elf",
            priority,
            budget,
        )

        # Initialise ARP requester config class
        FwArpResponderConfig.__init__(
            self,
            interface=net_interface.index,
            mac_addr=net_interface.mac_list,
            ip=net_interface.ip_int,
        )

    def finalise_config(self) -> None:
        assert self.mac_addr is not None
        assert len(self.mac_addr) == EthHwaddrLen
        assert self.ip is not None and self.ip != 0
