# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from sdfgen import SystemDescription
from pyfw.component_base import Component
from pyfw.config_structs import (
    FwWebserverConfig,
    FwWebserverInterfaceConfig,
)
from pyfw.constants import (
    interfaces,
    webserver_tx_interface_idx,
)

SDF_Channel = SystemDescription.Channel

class Webserver(Component, FwWebserverConfig):
    def __init__(
        self,
        priority: int = 1,
        budget: int = 20000,
        stack_size: int = 0x10000,
    ) -> None:
        # Initialise base component class
        super().__init__(
            "micropython",
            "micropython.elf",
            priority,
            budget,
            stack_size=stack_size,
        )

        # Create per-interface resources
        self._interfaces = []
        for iface in interfaces:
            self._interfaces.append(FwWebserverInterfaceConfig(
                iface.mac_list,
                iface.ip_int,
                iface.name,
                [],
                None,
                None)
        )

        # Initialise Router config class
        FwWebserverConfig.__init__(
            self,
            self._interfaces,
            None,
            None,
            webserver_tx_interface_idx,
        )

    def finalize_config(self) -> FwWebserverConfig:
        # TODO: Finish checking assertions
        assert self.router is not None
        assert self.arp_queue is not None
        return self
