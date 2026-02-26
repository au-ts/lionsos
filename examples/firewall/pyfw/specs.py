# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause
from dataclasses import dataclass
from typing import List, Optional, Tuple
from sdfgen import SystemDescription, Sddf, DeviceTree
from pyfw.config_structs import (
    RegionResource,
    DeviceRegionResource,
    FwConnectionResource,
    FwArpConnection,
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


class ReMappableRegion:
    """MR that can be mapped into multiple PDs via .map() calls."""

    def __init__(self, *, name, mr=None, size=0, category=""):
        self.name = name
        assert(mr != None)
        self.mr = mr
        self.size = size or mr.size
        self.category = category

    def get_mr(self) -> MemoryRegion:
        """Retrieve the Memory Region"""
        return self.mr

    def map(self, *, pd, perms="rw") -> RegionResource:
        """Map the MR into pd with given perms. Returns RegionResource."""
        pd_map = fw_map(pd, self.mr, perms)
        return fw_resource(pd_map, self.size)

    def map_device(self, *, pd, perms="rw") -> DeviceRegionResource:
        """Map the MR into pd with given perms. Returns DeviceRegionResource."""
        pd_map = fw_map(pd, self.mr, perms)
        return fw_device_resource(pd_map, self.mr)


class QueueConnection:
    """Queue MR + channel between two PDs."""

    def __init__(self, *, sdf, name, src_pd, dst_pd, capacity, queue_size,
                 name_suffix="", category="", interface_index=None):
        self.category = category
        self.interface_index = interface_index

        queue_name = (
            "fw_queue_" + src_pd.name + "_" + dst_pd.name + name_suffix
        )
        queue = MemoryRegion(sdf, queue_name, queue_size)
        sdf.add_mr(queue)

        src_map = fw_map(src_pd, queue, "rw")
        src_region = fw_resource(src_map, queue_size)

        dst_map = fw_map(dst_pd, queue, "rw")
        dst_region = fw_resource(dst_map, queue_size)

        ch = Channel(src_pd, dst_pd)
        sdf.add_channel(ch)

        self.src = FwConnectionResource(queue=src_region, capacity=capacity, ch=ch.pd_a_id)
        self.dst = FwConnectionResource(queue=dst_region, capacity=capacity, ch=ch.pd_b_id)


class ArpConnection:
    """Request queue + response queue + channel (bidirectional)."""

    def __init__(self, *, sdf, name, pd1, pd2, capacity, queue_size,
                 category="arp", interface_index=None):
        self.category = category
        self.interface_index = interface_index

        # Request queue
        req_queue_name = "fw_req_queue_" + pd1.name + "_" + pd2.name
        req_queue = MemoryRegion(sdf, req_queue_name, queue_size)
        sdf.add_mr(req_queue)
        pd1_req_map = fw_map(pd1, req_queue, "rw")
        pd1_req_region = fw_resource(pd1_req_map, queue_size)
        pd2_req_map = fw_map(pd2, req_queue, "rw")
        pd2_req_region = fw_resource(pd2_req_map, queue_size)

        # Response queue
        res_queue_name = "fw_res_queue_" + pd1.name + "_" + pd2.name
        res_queue = MemoryRegion(sdf, res_queue_name, queue_size)
        sdf.add_mr(res_queue)
        pd1_res_map = fw_map(pd1, res_queue, "rw")
        pd1_res_region = fw_resource(pd1_res_map, queue_size)
        pd2_res_map = fw_map(pd2, res_queue, "rw")
        pd2_res_region = fw_resource(pd2_res_map, queue_size)

        ch = Channel(pd1, pd2)
        sdf.add_channel(ch)

        self.pd1 = FwArpConnection(
            request=pd1_req_region, response=pd1_res_region, capacity=capacity, ch=ch.pd_a_id
        )
        self.pd2 = FwArpConnection(
            request=pd2_req_region, response=pd2_res_region, capacity=capacity, ch=ch.pd_b_id
        )


class PairedRegion:
    """New MR shared between exactly two PDs."""

    def __init__(self, *, sdf, name, size, owner_pd, peer_pd,
                 owner_perms="rw", peer_perms="r",
                 category="", interface_index=None):
        self.size = size
        self.category = category
        self.interface_index = interface_index

        region_name = name + "_" + owner_pd.name + "_" + peer_pd.name
        mr = MemoryRegion(sdf, region_name, size)
        sdf.add_mr(mr)
        owner_map = fw_map(owner_pd, mr, owner_perms)
        self.owner = fw_resource(owner_map, size)
        peer_map = fw_map(peer_pd, mr, peer_perms)
        self.peer = fw_resource(peer_map, size)

class PrivateRegion:
    """Private MR for a single PD."""

    def __init__(self, *, sdf, name, size, pd,
                 perms="rw", category="", interface_index=None):
        self.size = size
        self.category = category
        self.interface_index = interface_index

        region_name = name + "_" + pd.name
        mr = MemoryRegion(sdf, region_name, size)
        sdf.add_mr(mr)
        pd_map = fw_map(pd, mr, perms)
        self.resource = fw_resource(pd_map, size)


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
