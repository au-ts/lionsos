# Copyright 2026, UNSW SPDX-License-Identifier: BSD-2-Clause

from typing import Optional
from sdfgen import SystemDescription, Sddf, DeviceTree
from pyfw.constants import BuildConstants
from build.config_structs import (
    RegionResource,
    DeviceRegionResource,
)

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

class FirewallMemoryRegion:
    """Unified memory region: create MR, add to SDF, map into PDs."""

    def __init__(self, name, size, physical=False):
        self.mr = MemoryRegion(BuildConstants.sdf(), name, size, physical=physical)
        self.size = size
        self.physical = physical
        BuildConstants.sdf().add_mr(self.mr)

    def map(self, pd, perms="rw") -> RegionResource:
        """Map the MR into pd with given perms. Returns RegionResource."""
        pd_map = Map(self.mr, pd.get_map_vaddr(self.mr), perms=perms)
        pd.add_map(pd_map)
        return RegionResource(vaddr=pd_map.vaddr, size=self.size)

    def map_device(self, pd, perms="rw") -> DeviceRegionResource:
        """Map the physical MR into pd with given perms. Returns DeviceRegionResource."""
        assert self.physical
        pd_map = Map(self.mr, pd.get_map_vaddr(self.mr), perms=perms)
        pd.add_map(pd_map)
        region_resource = RegionResource(vaddr=pd_map.vaddr, size=self.size)
        return DeviceRegionResource(region=region_resource, io_addr=self.mr.paddr)


class TrackedNet:
    """Wrapper around the Sddf.Net object"""

    def __init__(
        self,
        ethernet_node: DeviceTree.Node,
        driver: ProtectionDomain,
        virt_tx: ProtectionDomain,
        virt_rx: ProtectionDomain,
        rx_dma_region: MemoryRegion,
        *,
        interface_index: int,
    ):
        self._net = Sddf.Net(
            BuildConstants.sdf(), ethernet_node, driver, virt_tx, virt_rx, rx_dma_region
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
