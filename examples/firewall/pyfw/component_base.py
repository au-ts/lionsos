# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

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
        priority: int,
        budget: int = 0,
        period: int = 0,
        stack_size: int = 0,
    ) -> None:
        self.pd = ProtectionDomain(
            name,
            elf,
            priority=priority,
            budget=budget or None,
            period=period or None,
            stack_size=stack_size or None,
        )

    @property
    def name(self) -> str:
        return self.pd.name
