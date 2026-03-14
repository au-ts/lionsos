# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from abc import ABC, abstractmethod
from sdfgen import SystemDescription
from pyfw.config_structs import Serializable

ProtectionDomain = SystemDescription.ProtectionDomain

def encode_iface_name(name: str) -> str:
      return name.encode("ascii", "ignore")[:63].decode("ascii")

# CALLUM: the finalise_config method is something that we want to run before output a serialisation, thus we can force any subclass of Component to implement finalise config method. Then the generated serialisation method can call this before outputting bytes guaranteeing our serialisation is valid
class Component(ABC, Serializable):
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

    @abstractmethod
    def finalise_config(self) -> None:
        pass
