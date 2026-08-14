# Copyright 2026, UNSW SPDX-License-Identifier: BSD-2-Clause

from sdfgen import SystemDescription
from pyfw.component_base import Component
from pyfw.constants import (
    BuildConstants,
    icmp_queue_buffer,
    icmp_queue_region,
    supported_protocols,
)
from pyfw.specs import FirewallMemoryRegion
from config_structs import (
    RegionResource,
    FwConnectionResource,
    FwIcmpModuleInterfaceConfig,
    FwIcmpModuleConfig,
)

SDF_Channel = SystemDescription.Channel

class IcmpModule(Component, FwIcmpModuleConfig):
    def __init__(
        self,
        priority: int = 100,
        budget: int = 20000,
    ) -> None:
        # Initialise base component class
        super().__init__(
            "icmp_module",
            "icmp_module.elf",
            priority,
            budget
        )

        # Initialise ICMP module config class
        FwIcmpModuleConfig.__init__(
            self,
            interfaces=[
                FwIcmpModuleInterfaceConfig(
                    mac_addr=interface.mac_list,
                    ip=interface.ip_int,
                    filters=[],
                )
                for interface in BuildConstants.interfaces()
            ],
            router=None,
        )

    def connect_router(self, router: Component) -> FwConnectionResource:
        # Create ICMP queue
        icmp_queue = FirewallMemoryRegion(
            "fw_queue_" + self.name + router.name,
            icmp_queue_region.region_size,
        )

        # Create channel
        ch = SDF_Channel(self.pd, router.pd)
        BuildConstants.sdf().add_channel(ch)

        # Update ICMP module config
        self.router = FwConnectionResource(
            queue=icmp_queue.map(self.pd, "rw"),
            capacity=icmp_queue_buffer.capacity,
            ch=ch.pd_a_id,
        )

        # Return router config
        return FwConnectionResource(
            queue=icmp_queue.map(router.pd, "rw"),
            capacity=icmp_queue_buffer.capacity,
            ch=ch.pd_b_id,
        )

    def connect_filter(self, ip_filter: Component, interface_idx: int, reject_support: bool) -> FwConnectionResource:
        assert self.interfaces is not None
        assert interface_idx >= 0 and interface_idx < len(self.interfaces)

        if reject_support:
            # Create ICMP queue
            icmp_queue = FirewallMemoryRegion(
                "fw_queue_" + self.name + ip_filter.name,
                icmp_queue_region.region_size,
            )

            # Create channel
            ch = SDF_Channel(self.pd, ip_filter.pd)
            BuildConstants.sdf().add_channel(ch)

            icmp_module_connection = FwConnectionResource(
                queue=icmp_queue.map(self.pd, "rw"),
                capacity=icmp_queue_buffer.capacity,
                ch=ch.pd_a_id
            )

            filter_connection = FwConnectionResource(
                queue=icmp_queue.map(ip_filter.pd, "rw"),
                capacity=icmp_queue_buffer.capacity,
                ch=ch.pd_b_id
            )

        else:
            # Create "dummy" firewall connection objects
            icmp_module_connection = FwConnectionResource(
                queue=RegionResource(vaddr=0, size=0),
                capacity=0,
                ch=0
            )

            filter_connection = icmp_module_connection

        # Update ICMP module config
        assert self.interfaces[interface_idx].filters is not None
        self.interfaces[interface_idx].filters.append(icmp_module_connection)

        # Return filter config
        return filter_connection

    def finalise_config(self) -> None:
        assert self.interfaces is not None and len(self.interfaces) == len(BuildConstants.interfaces())
        for iface in self.interfaces:
            assert iface.filters is not None and len(iface.filters) == len(supported_protocols)
