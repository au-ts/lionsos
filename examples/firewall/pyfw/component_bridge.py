# Copyright 2026, UNSW SPDX-License-Identifier: BSD-2-Clause

from pyfw.component_base import Component
from pyfw.component_net_interface import NetworkInterface

class Bridge(Component):
    def __init__(
        self,
        net_interface: NetworkInterface,
        priority: int,
        budget: int = 20000,
    ) -> None:
        # Initialise base component class
        super().__init__(
            f"firewall_bridge{net_interface.index}",
            f"firewall_bridge{net_interface.index}.elf",
            priority,
            budget,
        )

    def finalise_config(self) -> None:
        pass
