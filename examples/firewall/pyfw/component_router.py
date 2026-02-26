# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from typing import List, Optional

from sdfgen import SystemDescription

from pyfw.component_base import Component
from pyfw.config_structs import (
    FwArpConnection,
    FwConnectionResource,
    FwRouterConfig,
    FwRouterInterface,
    FwRoutingEntry,
    FwWebserverRouterConfig,
    RegionResource,
)


class RouterInterface:
    def __init__(self) -> None:
        self.rx_free: Optional[FwConnectionResource] = None
        self.tx_active: Optional[FwConnectionResource] = None
        self.data: Optional[RegionResource] = None
        self.arp_queue: Optional[FwArpConnection] = None
        self.arp_cache: Optional[RegionResource] = None
        self.arp_cache_capacity = 0
        self.filters: List[FwConnectionResource] = []
        self.mac_addr: Optional[List[int]] = None
        self.ip = 0
        self.subnet = 0

    def set_rx_free(self, queue: RegionResource, capacity: int, ch: int) -> None:
        self.rx_free = FwConnectionResource(queue=queue, capacity=capacity, ch=ch)

    def set_tx_active(self, queue: RegionResource, capacity: int, ch: int) -> None:
        self.tx_active = FwConnectionResource(queue=queue, capacity=capacity, ch=ch)

    def set_data(self, resource: RegionResource) -> None:
        self.data = resource

    def set_arp_queue(
        self,
        request: RegionResource,
        response: RegionResource,
        capacity: int,
        ch: int,
    ) -> None:
        self.arp_queue = FwArpConnection(
            request=request,
            response=response,
            capacity=capacity,
            ch=ch,
        )

    def set_arp_cache(self, resource: RegionResource, capacity: int) -> None:
        self.arp_cache = resource
        self.arp_cache_capacity = capacity

    def set_network_info(self, mac_addr: List[int], ip: int, subnet: int) -> None:
        self.mac_addr = mac_addr
        self.ip = ip
        self.subnet = subnet

    def add_filter(self, queue: RegionResource, capacity: int, ch: int) -> None:
        self.filters.append(FwConnectionResource(queue=queue, capacity=capacity, ch=ch))

    def to_struct(self) -> FwRouterInterface:
        assert self.rx_free is not None
        assert self.tx_active is not None
        assert self.data is not None
        assert self.arp_queue is not None
        assert self.arp_cache is not None
        assert self.mac_addr is not None
        assert self.arp_cache_capacity > 0
        assert len(self.filters) > 0
        assert len(self.mac_addr) > 0
        assert self.ip != 0
        assert self.subnet != 0
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
    def __init__(
        self,
        sdf: SystemDescription,
        priority: int = 97,
        budget: int = 20000,
    ) -> None:
        super().__init__("routing", "routing.elf", sdf, priority, budget=budget)
        self._interfaces: List[RouterInterface] = []
        self._packet_queue: Optional[RegionResource] = None
        self._packet_waiting_capacity = 0

        self._webserver_routing_ch: Optional[int] = None
        self._webserver_routing_table: Optional[RegionResource] = None
        self._webserver_routing_table_capacity = 0
        self._webserver_rx_conn: Optional[FwConnectionResource] = None

        self._initial_routes: List[FwRoutingEntry] = []
        self._icmp_conn: Optional[FwConnectionResource] = None

    def create_interface(self) -> RouterInterface:
        ri = RouterInterface()
        self._interfaces.append(ri)
        return ri

    def set_packet_queue(self, resource: RegionResource, capacity: int) -> None:
        self._packet_queue = resource
        self._packet_waiting_capacity = capacity

    def set_webserver_config(
        self,
        routing_ch: int,
        routing_table: RegionResource,
        routing_table_capacity: int,
    ) -> None:
        self._webserver_routing_ch = routing_ch
        self._webserver_routing_table = routing_table
        self._webserver_routing_table_capacity = routing_table_capacity

    def set_webserver_rx(self, queue: RegionResource, capacity: int, ch: int) -> None:
        self._webserver_rx_conn = FwConnectionResource(queue=queue, capacity=capacity, ch=ch)

    def set_icmp_connection(self, queue: RegionResource, capacity: int, ch: int) -> None:
        self._icmp_conn = FwConnectionResource(queue=queue, capacity=capacity, ch=ch)

    def add_initial_route(self, ip: int, subnet: int, interface: int, next_hop: int) -> None:
        self._initial_routes.append(FwRoutingEntry(ip,subnet,interface,next_hop))

    def finalize_config(self) -> FwRouterConfig:
        assert self._packet_queue is not None
        assert self._webserver_routing_ch is not None
        assert self._webserver_routing_table is not None
        assert self._webserver_routing_table_capacity > 0
        assert self._webserver_rx_conn is not None
        assert self._icmp_conn is not None
        assert len(self._interfaces) >= 2
        assert self._initial_routes is not None
        self.config = FwRouterConfig(
            interfaces=[ri.to_struct() for ri in self._interfaces],
            packet_queue=self._packet_queue,
            packet_queue_capacity=self._packet_waiting_capacity,
            webserver=FwWebserverRouterConfig(
                routing_ch=self._webserver_routing_ch,
                routing_table=self._webserver_routing_table,
                routing_table_capacity=self._webserver_routing_table_capacity,
                rx_active=self._webserver_rx_conn,
            ),
            initial_routes=self._initial_routes,
            icmp_module=self._icmp_conn,
        )
        return self.config
