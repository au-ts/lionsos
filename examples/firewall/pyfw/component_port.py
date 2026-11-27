# Copyright 2026, UNSW SPDX-License-Identifier: BSD-2-Clause

from dataclasses import dataclass
from pathlib import Path

from config_structs import NetDriverConfig
from sdfgen import SystemDescription
from pyfw.specs import NetworkDevice


@dataclass
class FirewallPort:
    board_ethernet_idx: int
    driver: SystemDescription.ProtectionDomain
    out_dir: str
    driver_config: NetDriverConfig
    device_system: NetworkDevice

    def connect(self) -> None:
        assert self.device_system.connect()
        self.device_system.serialise_config(self.out_dir)
        Path(self.out_dir, "net_driver.data").write_bytes(self.driver_config.serialise())
