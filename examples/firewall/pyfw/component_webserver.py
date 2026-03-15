# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from sdfgen import SystemDescription
from pyfw.component_base import Component
from pyfw.config_structs import (
    EthHwaddrLen,
    FwMaxFilters,
    FwMaxInterfaceNameLen,
    FwMaxInterfaces,
    FwWebserverConfig,
    FwWebserverInterfaceConfig,
)
from pyfw.constants import (
    interfaces,
    supported_protocols,
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
        self._interfaces: list[FwWebserverInterfaceConfig] = []
        for iface in interfaces:
            self._interfaces.append(
                FwWebserverInterfaceConfig(
                    mac_addr=iface.mac_list,
                    ip=iface.ip_int,
                    name=iface.name,
                    filters=[],
                    data=None,
                    rx_free=None,
                )
            )

        # Initialise Router config class
        FwWebserverConfig.__init__(
            self,
            interfaces=self._interfaces,
            router=None,
            arp_queue=None,
            tx_interface=webserver_tx_interface_idx,
        )

    def finalise_config(self) -> None:
        assert self.interfaces is not None
        assert len(self.interfaces) == len(interfaces)
        assert len(self.interfaces) <= FwMaxInterfaces
        assert self.router is not None
        assert self.arp_queue is not None

        for iface in self.interfaces:
            assert iface.mac_addr is not None
            assert len(iface.mac_addr) == EthHwaddrLen
            assert iface.ip is not None and iface.ip != 0
            assert iface.name != ""
            assert iface.name is not None
            assert len(iface.name) <= FwMaxInterfaceNameLen
            assert iface.data is not None
            assert iface.rx_free is not None
            assert iface.filters is not None
            assert len(iface.filters) == len(supported_protocols)
            assert len(iface.filters) <= FwMaxFilters
