# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from sdfgen import SystemDescription
from pyfw.component_base import Component
from pyfw.config_structs import (
    FwConnectionResource,
    FwRouterConfig,
    FwRouterInterface,
    FwRoutingEntry,
    FwWebserverRouterConfig,
)
from pyfw.constants import (
    NetworkInterface,
    interfaces,
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

# TODO: Should probably remove this class and use the config structs class directly...
class RouterInterface(FwRouterInterface):
    """Per-interface router configuration."""
    def __init__(self, net_interface: NetworkInterface) -> None:
        # Initialise router interface config class
        super().__init__(
            net_interface.mac_list,
            net_interface.ip_int,
            net_interface.subnet_bits,
            None,
            None,
            None,
            None,
            None,
            arp_cache_buffer.capacity,
            [],
        )

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
        self._interfaces = []
        self._packet_queue = []
        self._initial_routes = []
        for iface in interfaces:

            self._interfaces.append(RouterInterface(iface))

            # Create packet waiting memory pools
            packet_waiting_mr = FirewallMemoryRegion(
                "arp_packet_queue_" + self.name + f"{iface.index}",
                arp_packet_queue_region.region_size,
            )

            self._packet_queue.append(packet_waiting_mr.map(self.pd, "rw"))

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
            # TODO: Wanted one big region here, split inside the code... if we dont do this, then it should be inside the per-interface config
            self._packet_queue,
            arp_packet_queue_buffer.capacity,
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

        # Webserver needs read-only access to routing table
        webserver_config = FwWebserverRouterConfig(
            None,
            self._routing_table_mr.map(webserver.pd, "r"),
            routing_table_buffer.capacity,
            None)

        # Router transmits to the webserver
        queue = FirewallMemoryRegion(
            "fw_queue_" + self.name + "_" + webserver.name,
            dma_buffer_queue_region.region_size,
        )

        # Create channel for notifying upon transmitting packets
        ch = SDF_Channel(self.pd, webserver.pd)
        pyfw.constants.sdf.add_channel(ch)

        self.webserver.rx_active = FwConnectionResource(
            queue.map(self.pd, "rw"),
            dma_buffer_queue.capacity,
            ch.pd_a_id,
        )

        webserver_config.rx_active = FwConnectionResource(
            queue.map(webserver.pd, "rw"),
            dma_buffer_queue.capacity,
            ch.pd_b_id,
        )

        # Router needs chanel to webserver for routing table updates
        update_ch = SDF_Channel(self.pd, webserver.pd, pp_b=True)
        pyfw.constants.sdf.add_channel(update_ch)

        self.webserver.routing_ch = update_ch.pd_a_id
        webserver_config.routing_ch = update_ch.pd_b_id

        return webserver_config


    def finalize_config(self) -> FwRouterConfig:
        # TODO: Finish checking assertions
        assert len(self.packet_queue) > 0
        assert self.packet_queue_capacity is not None
        return self
