# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from sdfgen import SystemDescription
from pyfw.component_base import Component
from pyfw.config_structs import (
    EthHwaddrLen,
    FwConnectionResource,
    FwMaxFilters,
    FwMaxInitialRoutes,
    FwMaxInterfaces,
    FwRouterConfig,
    FwRouterInterface,
    FwRoutingEntry,
    FwWebserverRouterConfig,
)
from pyfw.constants import (
    interfaces,
    supported_protocols,
    arp_packet_queue_buffer,
    arp_packet_queue_region,
    arp_cache_buffer,
    routing_table_buffer,
    routing_table_region,
    dma_buffer_queue,
    dma_buffer_queue_region,
)
import pyfw.constants
from pyfw.specs import FirewallMemoryRegion

SDF_Channel = SystemDescription.Channel

class Router(Component, FwRouterConfig):
    def __init__(
        self,
        priority: int = 97,
        budget: int = 20000,
    ) -> None:
        # Initialise base component class
        super().__init__(
            "routing",
            "routing.elf",
            priority,
            budget
        )

        # Create the routing table
        self._routing_table_mr = FirewallMemoryRegion(
            "routing_table_" + self.name,
            routing_table_region.region_size,
        )

        # Create per-interface resources
        self._interfaces: list[FwRouterInterface] = []
        self._initial_routes: list[FwRoutingEntry] = []
        for iface in interfaces:
            # Create packet waiting memory pools
            packet_waiting_mr = FirewallMemoryRegion(
                "arp_packet_queue_" + self.name + f"{iface.index}",
                arp_packet_queue_region.region_size,
            )

            self._interfaces.append(
                FwRouterInterface(
                    mac_addr=iface.mac_list,
                    ip=iface.ip_int,
                    subnet=iface.subnet_bits,
                    rx_free=None,
                    tx_active=None,
                    data=None,
                    arp_queue=None,
                    arp_cache=None,
                    arp_cache_capacity=arp_cache_buffer.capacity,
                    filters=[],
                    packet_queue=packet_waiting_mr.map(self.pd, "rw"),
                    packet_queue_capacity=arp_packet_queue_buffer.capacity,
                )
            )

            # Create a direct route for interface subnets
            self._initial_routes.append(FwRoutingEntry(
                iface.ip_int,
                iface.subnet_bits,
                iface.index,
                0
            ))

            # TODO: Append additional initial routes which can be set in constants.py here

        # Initialise Router config class
        FwRouterConfig.__init__(
            self,
            self._interfaces,
            FwWebserverRouterConfig(
                None,
                self._routing_table_mr.map(self.pd, "rw"),
                routing_table_buffer.capacity,
                None),
            self._initial_routes,
            None,
        )

    def connect_webserver(
        self,
        webserver: Component
    ) -> FwWebserverRouterConfig:
        assert self.webserver is not None

        # Webserver needs read-only access to routing table
        router_webserver_config = self.webserver
        webserver_router_config = FwWebserverRouterConfig(
            None,
            self._routing_table_mr.map(webserver.pd, "r"),
            routing_table_buffer.capacity,
            None,
        )

        # Router transmits to the webserver
        queue = FirewallMemoryRegion(
            "fw_queue_" + self.name + "_" + webserver.name,
            dma_buffer_queue_region.region_size,
        )

        # Create channel for notifying upon transmitting packets
        ch = SDF_Channel(self.pd, webserver.pd)
        pyfw.constants.sdf.add_channel(ch)

        router_webserver_config.rx_active = FwConnectionResource(
            queue.map(self.pd, "rw"),
            dma_buffer_queue.capacity,
            ch.pd_a_id,
        )

        webserver_router_config.rx_active = FwConnectionResource(
            queue.map(webserver.pd, "rw"),
            dma_buffer_queue.capacity,
            ch.pd_b_id,
        )

        # Router needs channel to webserver for routing table updates
        update_ch = SDF_Channel(self.pd, webserver.pd, pp_b=True)
        pyfw.constants.sdf.add_channel(update_ch)

        router_webserver_config.routing_ch = update_ch.pd_a_id
        webserver_router_config.routing_ch = update_ch.pd_b_id

        return webserver_router_config


    def finalise_config(self) -> None:
        assert len(self.interfaces) == len(interfaces)
        assert len(self.interfaces) <= FwMaxInterfaces
        assert len(self.initial_routes) >= len(interfaces)
        assert len(self.initial_routes) <= FwMaxInitialRoutes
        assert self.webserver is not None
        assert self.webserver.routing_ch is not None
        assert self.webserver.routing_table is not None
        assert self.webserver.routing_table_capacity is not None
        assert self.webserver.rx_active is not None
        assert self.icmp_module is not None

        for iface in self.interfaces:
            assert len(iface.mac_addr) == EthHwaddrLen
            assert iface.ip is not None and iface.ip != 0
            assert iface.subnet is not None and iface.subnet > 0
            assert iface.rx_free is not None
            assert iface.tx_active is not None
            assert iface.data is not None
            assert iface.arp_queue is not None
            assert iface.arp_cache is not None
            assert len(iface.filters) == len(supported_protocols)
            assert len(iface.filters) <= FwMaxFilters
            assert iface.packet_queue is not None
