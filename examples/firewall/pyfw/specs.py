# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause
from dataclasses import dataclass
from typing import List, Optional, Tuple
from sdfgen import SystemDescription, Sddf, DeviceTree
from pyfw.config_structs import (
    RegionResource,
    DeviceRegionResource,
)

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel


def fw_map(pd, mr, perms):
    """Map a memory region into a protection domain, return the Mapping."""
    pd_map = Map(mr, pd.get_map_vaddr(mr), perms=perms)
    pd.add_map(pd_map)
    return pd_map


def fw_resource(pd_map, size):
    """Create a RegionResource from a Mapping and size."""
    return RegionResource(vaddr=pd_map.vaddr, size=size)


def fw_device_resource(pd_map, mr):
    """Create a DeviceRegionResource from a Mapping and its physical MR."""
    return DeviceRegionResource(region=fw_resource(pd_map, mr.size), io_addr=mr.paddr)


class FirewallMemoryRegion:
    """Unified memory region: create MR, add to SDF, map into PDs."""

    def __init__(self, sdf, name, size, physical=False):
        self.mr = MemoryRegion(sdf, name, size, physical=physical)
        self.size = size
        sdf.add_mr(self.mr)

    def map(self, pd, perms="rw"):
        """Map the MR into pd with given perms. Returns RegionResource."""
        pd_map = fw_map(pd, self.mr, perms)
        return fw_resource(pd_map, self.size)

    def map_device(self, pd, perms="rw"):
        """Map the MR into pd with given perms. Returns DeviceRegionResource."""
        pd_map = fw_map(pd, self.mr, perms)
        return fw_device_resource(pd_map, self.mr)


class TrackedNet:
    """Wrapper around the Sddf.Net object"""

    def __init__(
        self,
        sdf_obj: SystemDescription,
        ethernet_node: DeviceTree.Node,
        driver: ProtectionDomain,
        virt_tx: ProtectionDomain,
        virt_rx: ProtectionDomain,
        rx_dma_region: Optional[MemoryRegion],
        *,
        interface_index: int,
    ):
        self._net = Sddf.Net(
            sdf_obj, ethernet_node, driver, virt_tx, virt_rx, rx_dma_region
        )
        self._driver = driver
        self._virt_tx = virt_tx
        self._virt_rx = virt_rx
        self._interface_index = interface_index


    def add_client_with_copier(
        self,
        client: ProtectionDomain,
        copier: Optional[ProtectionDomain] = None,
        *,
        mac_addr: Optional[str] = None,
        rx: Optional[bool] = None,
        tx: Optional[bool] = None,
    ) -> None:
        self._net.add_client_with_copier(
            client, copier, mac_addr=mac_addr, rx=rx, tx=tx
        )

    def connect(self) -> bool:
        return self._net.connect()

    def serialise_config(self, output_dir: str) -> bool:
        return self._net.serialise_config(output_dir)
