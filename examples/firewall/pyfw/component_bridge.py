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
        # CD: Where is this initialised?
        assert self.ports is not None
        assert len(self.ports) < FwBridgeMaxPorts

        driver_rx, bridge_rx = connect_queues(f"{driver.name}_rx", driver, self.pd)
        driver_tx, bridge_tx = connect_queues(f"{driver.name}_tx", driver, self.pd)

        self.ports.append(FwBridgePort(rx=bridge_rx, tx=bridge_tx))

        return driver_rx, driver_tx

    # CD: This function name is not helpful to understand what it does. Connect
    # virtualisers perhaps?
    def connect_local(self, net_system: TrackedNet) -> None:
        # CD: What is driver_config? why would the virtualisers live in the "driver config"?
        driver_config = net_system.driver_config
        # CD: Okay, do you mean the net_driver_config? In this case, these are
        # the virtual addresses and queue specs of the driver-virt queues from
        # the *driver's perspective*. Why would the bridge require these in
        # order to map in queues?
        self.local_rx = net_connection(driver_config.virt_rx)
        self.local_tx = net_connection(driver_config.virt_tx)
        # CD: I think what you are doing here is "stealing" the mapping
        # generated for the driver and replacing it to be mapped into the
        # bridge?
        for mr in net_system.tx_data_regions:
            self.map_data(mr)

    def map_data(self, mr: SystemDescription.MemoryRegion) -> None:
        # CD: What do you mean if already mapped? Why would that be the case?
        """Map a packet pool region into the bridge's PD, read only. If already mapped, no-op."""
        # CD: Where is this initialised?
        assert self.data_regions is not None
        assert mr.paddr is not None
        io_addr = mr.paddr.value
        # CD: Why would there be duplicates in this case?
        if any(region.io_addr == io_addr for region in self.data_regions):
            return
        # CD: Net max clients applies to each *interface*. It is not a global
        # constant. So this assertion is not correct
        assert len(self.data_regions) < FwMaxInterfaces + SddfNetMaxClients

        # CD: I'm wondering if it's even worth giving the bridge access to all
        # of these regions, or whether it would have been a better design to
        # have the firewall net TX virt just pass along the MAC address directly
        # to the bridge? I.e. it could be bridge aware
        mapping = SystemDescription.Map(mr, self.pd.get_map_vaddr(mr), perms="r")
        self.pd.add_map(mapping)
        self.data_regions.append(
            DeviceRegionResource(
                region=RegionResource(vaddr=mapping.vaddr, size=mr.size),
                io_addr=io_addr,
            )
        )

    def finalise_config(self) -> None:
        # CD: The purpose of this function is to do things that need to be done
        # *after* other things have completed. For example, you may want to map
        # in all the Tx data regions *after* all the Tx net clients have been
        # added. It can also be used for assertions that ought to hold true by
        # the end of the metaprogram. It surprises me that this is empty
        pass


# CD: This function name is also a bit vague. Which queues? There are lots of
# queues in the firewall
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
    # CD: Where do you map these queues in??

    return driver_conn, bridge_conn


# CD: Why do you steal the driver's virtual address for the bridge's mapping?
# CD: Why are you using the NetConnectionResourceStruct here? This is really
# supposed to be only be used *within* the config structs module? Why does this
# function even exist? Why are you declaring a region resource here that you
# haven't created?
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
