# Copyright 2026, UNSW SPDX-License-Identifier: BSD-2-Clause

from config_structs import (
    DeviceRegionResource,
    FwBridgeConfig,
    FwBridgeMaxPorts,
    FwBridgePort,
    FwMaxInterfaces,
    NetConnectionResource,
    NetConnectionResourceStruct,
    RegionResource,
    SddfNetMaxClients,
)
from pyfw.component_base import Component
from pyfw.component_net_interface import NetworkInterface
from pyfw.constants import BuildConstants, dma_buffer_queue, sddf_net_queue_region
from pyfw.specs import FirewallMemoryRegion, TrackedNet
from sdfgen import SystemDescription


class Bridge(Component, FwBridgeConfig):
    def __init__(
        self,
        net_interface: NetworkInterface,
        priority: int,
        budget: int = 20000,
    ) -> None:
        super().__init__(
            f"firewall_bridge{net_interface.index}",
            f"firewall_bridge{net_interface.index}.elf",
            priority,
            budget,
        )
        FwBridgeConfig.__init__(
            self,
            mac_addr=net_interface.mac_list,
        )

    def connect_port(
        self, driver: SystemDescription.ProtectionDomain
    ) -> tuple[NetConnectionResource, NetConnectionResource]:
        assert self.ports is not None
        assert len(self.ports) < FwBridgeMaxPorts

        driver_rx, bridge_rx = connect_queues(f"{driver.name}_rx", driver, self.pd)
        driver_tx, bridge_tx = connect_queues(f"{driver.name}_tx", driver, self.pd)

        self.ports.append(FwBridgePort(rx=bridge_rx, tx=bridge_tx))

        return driver_rx, driver_tx

    def connect_local(self, net_system: TrackedNet) -> None:
        driver_config = net_system.driver_config
        self.local_rx = net_connection(driver_config.virt_rx)
        self.local_tx = net_connection(driver_config.virt_tx)
        for mr in net_system.tx_data_regions:
            self.map_data(mr)

    def map_data(self, mr: SystemDescription.MemoryRegion) -> None:
        """Map a packet pool region into the bridge's PD, read only. If already mapped, no-op."""
        assert self.data_regions is not None
        assert mr.paddr is not None
        io_addr = mr.paddr.value
        if any(region.io_addr == io_addr for region in self.data_regions):
            return
        assert len(self.data_regions) < FwMaxInterfaces + SddfNetMaxClients

        mapping = SystemDescription.Map(mr, self.pd.get_map_vaddr(mr), perms="r")
        self.pd.add_map(mapping)
        self.data_regions.append(
            DeviceRegionResource(
                region=RegionResource(vaddr=mapping.vaddr, size=mr.size),
                io_addr=io_addr,
            )
        )

    def finalise_config(self) -> None:
        pass


def connect_queues(
    name: str,
    driver: SystemDescription.ProtectionDomain,
    bridge: SystemDescription.ProtectionDomain,
) -> tuple[NetConnectionResource, NetConnectionResource]:
    free = FirewallMemoryRegion(f"{name}_free", sddf_net_queue_region.region_size)
    active = FirewallMemoryRegion(f"{name}_active", sddf_net_queue_region.region_size)

    channel = SystemDescription.Channel(driver, bridge)
    BuildConstants.sdf().add_channel(channel)

    driver_conn = NetConnectionResource(
        free_queue=free.map(driver, "rw"),
        active_queue=active.map(driver, "rw"),
        num_buffers=dma_buffer_queue.capacity,
        id=channel.pd_a_id,
    )
    bridge_conn = NetConnectionResource(
        free_queue=free.map(bridge, "rw"),
        active_queue=active.map(bridge, "rw"),
        num_buffers=dma_buffer_queue.capacity,
        id=channel.pd_b_id,
    )

    return driver_conn, bridge_conn


def net_connection(conn: NetConnectionResourceStruct) -> NetConnectionResource:
    return NetConnectionResource(
        free_queue=RegionResource(
            vaddr=conn.free_queue.vaddr, size=conn.free_queue.size
        ),
        active_queue=RegionResource(
            vaddr=conn.active_queue.vaddr, size=conn.active_queue.size
        ),
        num_buffers=conn.num_buffers,
        id=conn.id,
    )
