# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from sdfgen import SystemDescription
from pyfw.component_base import Component
from pyfw.config_structs import (
    FwConnectionResource,
    FwIcmpModuleConfig,
)
from pyfw.constants import (
    interfaces,
    icmp_queue_buffer,
    icmp_queue_region,
)
import pyfw.constants
from pyfw.specs import FirewallMemoryRegion

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
            len(interfaces),
            [interface.ip_int for interface in interfaces],
            None,
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
            icmp_queue.map(self.pd, "rw"),
            icmp_queue_buffer.capacity,
            ch.pd_a_id,
        )

        # Return router config
        return FwConnectionResource(
            icmp_queue.map(router.pd, "rw"),
            icmp_queue_buffer.capacity,
            ch.pd_b_id,
        )

    def finalize_config(self) -> FwIcmpModuleConfig:
        # TODO: Finish checking assertions
        assert self.router is not None
        return self
