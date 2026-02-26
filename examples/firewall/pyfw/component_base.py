# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from typing import Any

from sdfgen import SystemDescription

ProtectionDomain = SystemDescription.ProtectionDomain

def encode_iface_name(name: str) -> str:
      return name.encode("ascii", "ignore")[:63].decode("ascii")

class Component:
    """Base class for all system components."""

    def __init__(
        self,
        name: str,
        elf: str,
        sdf: SystemDescription,
        priority: int,
        budget: int = 0,
        period: int = 0,
        stack_size: int = 0,
    ) -> None:
        self.sdf = sdf
        self.pd = ProtectionDomain(
            name,
            elf,
            priority=priority,
            budget=budget or None,
            period=period or None,
            stack_size=stack_size or None,
        )
        self.config: Any

    @property
    def name(self) -> str:
        return self.pd.name

    def register(self) -> None:
        self.sdf.add_pd(self.pd)

    def finalize_config(self) -> Any:
        raise NotImplementedError
