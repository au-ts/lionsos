# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from typing import Dict, List, Optional, Tuple

from sdfgen import SystemDescription

from pyfw.component_base import Component, encode_iface_name
from pyfw.component_models import NetworkInterface
from pyfw.config_structs import (
    DeviceRegionResource,
    FwArpConnection,
    FwConnectionResource,
    FwWebserverConfig,
    FwWebserverFilterConfig,
    FwWebserverInterfaceConfig,
    FwWebserverRouterConfig,
    RegionResource,
)


class Webserver(Component):
    def __init__(
        self,
        sdf: SystemDescription,
        priority: int = 1,
        budget: int = 20000,
        stack_size: int = 0x10000,
    ) -> None:
        super().__init__(
            "micropython",
            "micropython.elf",
            sdf,
            priority,
            budget=budget,
            stack_size=stack_size,
        )
        self._rx_active: Optional[FwConnectionResource] = None
        self._arp_conn: Optional[FwArpConnection] = None

        self._router_routing_ch: Optional[int] = None
        self._router_routing_table: Optional[RegionResource] = None
        self._router_routing_table_capacity = 0

        self._interface_resources: Dict[int, Tuple[DeviceRegionResource, FwConnectionResource]] = {}
        self._filter_configs: Dict[int, Dict[int, FwWebserverFilterConfig]] = {}
        self._interface_configs: List[FwWebserverInterfaceConfig] = []
        self._interface_idx = 0

    def set_rx_active(self, queue: RegionResource, capacity: int, ch: int) -> None:
        self._rx_active = FwConnectionResource(queue=queue, capacity=capacity, ch=ch)

    def set_interface_resources(
        self,
        interface_idx: int,
        data: DeviceRegionResource,
        rx_free_queue: RegionResource,
        rx_free_capacity: int,
        rx_free_ch: int,
    ) -> None:
        self._interface_resources[interface_idx] = (
            data,
            FwConnectionResource(
                queue=rx_free_queue,
                capacity=rx_free_capacity,
                ch=rx_free_ch,
            ),
        )

    def set_arp_connection(
        self,
        request: RegionResource,
        response: RegionResource,
        capacity: int,
        ch: int,
    ) -> None:
        self._arp_conn = FwArpConnection(
            request=request,
            response=response,
            capacity=capacity,
            ch=ch,
        )

    def set_router_config(
        self,
        routing_ch: int,
        routing_table: RegionResource,
        routing_table_capacity: int,
    ) -> None:
        self._router_routing_ch = routing_ch
        self._router_routing_table = routing_table
        self._router_routing_table_capacity = routing_table_capacity

    def set_filter_config(
        self,
        interface_idx: int,
        protocol: int,
        ch: int,
        rules: RegionResource,
        rules_capacity: int,
    ) -> None:
        if interface_idx not in self._filter_configs:
            self._filter_configs[interface_idx] = {}
        self._filter_configs[interface_idx][protocol] = FwWebserverFilterConfig(
            protocol=protocol,
            ch=ch,
            rules=rules,
            rules_capacity=rules_capacity,
        )

    def add_interface_config(self, iface: NetworkInterface) -> None:
        assert iface.index in self._interface_resources
        assert iface.index in self._filter_configs
        data, rx_free = self._interface_resources[iface.index]
        filter_configs = self._filter_configs[iface.index]

        ws_filter_configs: List[FwWebserverFilterConfig] = []
        for proto in sorted(iface.filters.keys()):
            assert proto in filter_configs
            ws_filter_configs.append(filter_configs[proto])

        self._interface_configs.append(
            FwWebserverInterfaceConfig(
                mac_addr=iface.mac_list,
                ip=iface.ip_int,
                filters=ws_filter_configs,
                name=encode_iface_name(iface.name),
                data=data,
                rx_free=rx_free,
            )
        )

    def set_interface(self, idx: int) -> None:
        self._interface_idx = idx

    def finalize_config(self) -> FwWebserverConfig:
        assert self._rx_active is not None
        assert self._arp_conn is not None
        assert self._router_routing_ch is not None
        assert self._router_routing_table is not None
        assert self._router_routing_table_capacity > 0
        assert len(self._interface_configs) > 0
        self.config = FwWebserverConfig(
            interfaces=self._interface_configs,
            router=FwWebserverRouterConfig(
                routing_ch=self._router_routing_ch,
                routing_table=self._router_routing_table,
                routing_table_capacity=self._router_routing_table_capacity,
                rx_active=self._rx_active,
            ),
            arp_queue=self._arp_conn,
            tx_interface=self._interface_idx,
        )
        return self.config
