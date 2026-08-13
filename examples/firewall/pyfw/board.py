# Copyright 2026, UNSW SPDX-License-Identifier: BSD-2-Clause

from dataclasses import dataclass
from sdfgen import SystemDescription

@dataclass(frozen=True)
class Board:
    name: str
    arch: SystemDescription.Arch
    paddr_top: int
    serial: str
    timer: str
    ethernet: tuple[str, ...]

    def ethernet_node_path(self, idx: int) -> str:
        assert 0 <= idx < len(self.ethernet)
        return self.ethernet[idx]
