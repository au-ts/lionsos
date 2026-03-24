# Copyright 2026, UNSW SPDX-License-Identifier: BSD-2-Clause

from dataclasses import dataclass, field
from sdfgen import SystemDescription

@dataclass(frozen=True)
class Board:
    name: str
    arch: SystemDescription.Arch
    paddr_top: int
    serial: str
    timer: str
    ethernet0: str
    ethernet1: str

    def ethernet_node_path(self, slot: str) -> str:
        assert slot in ("ethernet0", "ethernet1")
        return getattr(self, slot)
