# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from typing import List

from sdfgen import SystemDescription

from pyfw.component_base import Component
from pyfw.config_structs import (
    DeviceRegionResource,
    FwConnectionResource,
    FwDataConnectionResource,
    FwNetVirtRxConfig,
    FwNetVirtTxConfig,
    RegionResource,
)


class NetVirtRx(Component):
    def __init__(self, iface_index: int, sdf: SystemDescription, priority: int) -> None:
        super().__init__(
            f"net_virt_rx{iface_index}",
            f"firewall_network_virt_rx{iface_index}.elf",
            sdf,
            priority,
        )
        self.iface_index = iface_index
        self._ethtypes: List[int] = []
        self._subtypes: List[int] = []
        self._free_clients: List[FwConnectionResource] = []

    def add_active_client(self, ethtype: int, subtype: int) -> None:
        self._ethtypes.append(ethtype)
        self._subtypes.append(subtype)

    def add_free_client(self, queue: RegionResource, capacity: int, ch: int) -> None:
        self._free_clients.append(
            FwConnectionResource(queue=queue, capacity=capacity, ch=ch)
        )

    def finalize_config(self) -> FwNetVirtRxConfig:
        assert len(self._free_clients) > 0
        assert len(self._ethtypes) > 0 and len(self._subtypes) > 0
        self.config = FwNetVirtRxConfig(
            interface=self.iface_index,
            active_client_ethtypes=self._ethtypes,
            active_client_subtypes=self._subtypes,
            free_clients=self._free_clients,
        )
        return self.config


class NetVirtTx(Component):
    def __init__(
        self,
        iface_index: int,
        sdf: SystemDescription,
        priority: int,
        budget: int = 20000,
    ) -> None:
        super().__init__(
            f"net_virt_tx{iface_index}",
            f"firewall_network_virt_tx{iface_index}.elf",
            sdf,
            priority,
            budget=budget,
        )
        self.iface_index = iface_index
        self._active_clients: List[FwConnectionResource] = []
        self._free_clients: List[FwDataConnectionResource] = []
        self._data_regions: List[DeviceRegionResource] = []

    def add_data_region(self, resource: DeviceRegionResource) -> None:
        self._data_regions.append(resource)

    def add_active_client(self, queue: RegionResource, capacity: int, ch: int) -> None:
        self._active_clients.append(
            FwConnectionResource(queue=queue, capacity=capacity, ch=ch)
        )

    def add_free_client(
        self,
        queue: RegionResource,
        capacity: int,
        ch: int,
        data: DeviceRegionResource,
    ) -> None:
        conn = FwConnectionResource(queue=queue, capacity=capacity, ch=ch)
        self._free_clients.append(FwDataConnectionResource(conn=conn, data=data))

    def finalize_config(self) -> FwNetVirtTxConfig:
        assert len(self._active_clients) > 0
        assert len(self._free_clients) > 0
        assert len(self._data_regions) > 0
        self.config = FwNetVirtTxConfig(
            interface=self.iface_index,
            active_clients=self._active_clients,
            data_regions=self._data_regions,
            free_clients=self._free_clients,
        )
        return self.config
