# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from sdfgen import SystemDescription
from pyfw.component_base import Component
from pyfw.config_structs import (
    FwConnectionResource,
    FwIcmpModuleInterfaceConfig,
    FwMaxFilters,
    FwMaxInterfaces,
    FwIcmpModuleConfig,
)
from pyfw.constants import (
    interfaces,
    icmp_queue_buffer,
    icmp_queue_region,
)
import pyfw.constants
from pyfw.specs import FirewallMemoryRegion
from pyfw.component_interface import NetworkInterface

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
                for interface in interfaces
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
        pyfw.constants.sdf.add_channel(ch)

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
    
    def connect_interface_filters(self, iface: NetworkInterface) -> None:
        assert self.interfaces is not None
        icmp_module_config = self.interfaces[iface.index]
        for filter in iface.filters.values():
            # Create ICMP queue
            icmp_queue = FirewallMemoryRegion(
                "fw_queue_" + self.name + filter.name, icmp_queue_region.region_size
            )

            # Channel between the icmp module and each filter
            channel = SDF_Channel(self.pd, filter.pd)
            pyfw.constants.sdf.add_channel(channel)

            # ICMP modules side of the connection
            assert icmp_module_config.filters is not None
            icmp_module_config.filters.append(FwConnectionResource(
                queue=icmp_queue.map(self.pd, "rw"),
                capacity=icmp_queue_buffer.capacity,
                ch=channel.pd_a_id
            ))

            # Filters side of the connection
            filter.icmp_module = FwConnectionResource(
                queue=icmp_queue.map(filter.pd, "rw"),
                capacity=icmp_queue_buffer.capacity,
                ch=channel.pd_b_id
            )
        

    def finalise_config(self) -> None:
        assert self.interfaces is not None
        assert len(self.interfaces) > 0
        assert len(self.interfaces) == len(interfaces)
        assert len(self.interfaces) <= FwMaxInterfaces
        assert self.router is not None
        for iface, iface_config in zip(interfaces, self.interfaces):
            assert iface_config.filters is not None
            assert len(iface_config.filters) == len(iface.filters)
            assert len(iface_config.filters) <= FwMaxFilters
            for filter_component in iface.filters.values():
                assert filter_component.icmp_module is not None
