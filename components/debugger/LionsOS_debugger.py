# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
from dataclasses import dataclass
from typing import List, Union, Optional
from sdfgen import SystemDescription as SD, Sddf
from importlib.metadata import version
from board import BOARDS
from subprocess import run
from enum import Enum, auto
import itertools

class Debugger():
    class Backend(Enum):
        SDDF_SERIAL = auto()
        SDDF_NET = auto()

    @dataclass
    class SerialBackend:
        serial_system: Sddf.Serial
        other_systems: Optional[List]=None

        def add_client(self, debugger: SD.ProtectionDomain):
            serial_system.add_client(debugger)
            for sys in other_systems:
                sys.add_client(debugger)

    @dataclass
    class NetBackend:
        sdf: SD
        net_system: Sddf.Net
        copier: SD.ProtectionDomain
        timer_system: Sddf.Timer
        other_systems: Optional[List]=None

        def add_client(self, debugger: SD.ProtectionDomain):
            self.net_system.add_client_with_copier(debugger, self.copier)
            self.timer_system.add_client(debugger)
            for sys in self.other_systems:
                sys.add_client(debugger)
            return Sddf.Lwip(self.sdf, self.net_system, debugger)

    ValidBackends = SerialBackend | NetBackend;

    def __init__(
            self,
            sdf: SD,
            backend: ValidBackends,
            *,
            name: str = "debugger",
            program_image: str = "debugger.elf",
            priority: int | None = None,
            budget: int | None = None,
            period: int | None = None,
            passive: bool | None = None,
            stack_size: int | None = None,
            cpu: int | None = None
        ):
        if stack_size is None:
            stack_size = 0x100000;
        self._name=name
        self._program_image=program_image
        self._priority=priority
        self._budget=budget
        self._period=period
        self._passive=passive
        self._stack_size=stack_size
        self._cpu=cpu
        self._sdf = sdf
        self._backend = backend
        self._debuggees = []

        match type(backend):
            case self.SerialBackend:
                self.backend_type = self.Backend.SDDF_SERIAL
            case self.NetBackend:
                self.backend_type = self.Backend.SDDF_NET
            case _:
                raise TypeError(f"Invalid backend type of {type(backend)}, expected {ValidBackends}")

        self._small_mapping_region =    SD.MemoryRegion(sdf,    "small_region", 0x1000)
        self._sdf.add_mr(self._small_mapping_region)
        self._small_map =   SD.Map(self._small_mapping_region, 0x900000,    "rw", setvar_vaddr="small_mapping_mr")

        self._large_mapping_region =    SD.MemoryRegion(sdf,    "large_region", 0x200000)
        self._sdf.add_mr(self._large_mapping_region)
        self._large_map =   SD.Map(self._large_mapping_region, 0xa00000,    "rw", setvar_vaddr="large_mapping_mr")
        self._pts = SD.PageTables(setvar="table_metadata")
        self._debuggee_counter = itertools.count().__next__

    def add_debuggee(self, pd: SD.ProtectionDomain):
        self._debuggees.append(pd)

    def add_debuggees(self, pds: List[SD.ProtectionDomain]):
        for pd in pds:
            self.add_debuggee(pd)

    def finalise(self):
        debuggerPd = SD.ProtectionDomain(
            name=self._name,
            program_image=self._program_image,
            priority=self._priority,
            budget=self._budget,
            period=self._period,
            passive=self._passive,
            stack_size=self._stack_size,
            cpu=self._cpu,
        )
        for i, debuggee in enumerate(self._debuggees):
            debuggerPd.add_child_pd(debuggee);
            self._pts.add_entry(debuggee.name, index=i)
        debuggerPd.set_page_tables(self._pts)
        debuggerPd.add_map(self._large_map)
        debuggerPd.add_map(self._small_map)
        res = self._backend.add_client(debuggerPd)
        return (debuggerPd, res)
