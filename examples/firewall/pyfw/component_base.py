# Copyright 2026, UNSW SPDX-License-Identifier: BSD-2-Clause

from abc import ABC, abstractmethod
from sdfgen import SystemDescription
from build.config_structs import Serializable

ProtectionDomain = SystemDescription.ProtectionDomain
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
    # We force all sub-classes of this base class to implement a finalise_config
    # method, which allows the class creator to ensure that the fields of each
    # class object have been initialised correctly prior to serialisation. The
    # serialisation method which is ultimately called when serialising the
    # configuration struct is responsible for calling each object's
    # finalise_config method prior to serialisation.
    def finalise_config(self) -> None:
        pass
