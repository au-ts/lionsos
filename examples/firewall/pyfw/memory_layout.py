# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from __future__ import annotations

import subprocess
from os import path
from typing import Callable, Optional

PAGE_SIZE = 0x1000
UINT64_BYTES = 8


def round_up_to_page(size: int) -> int:
    if size < PAGE_SIZE:
        return PAGE_SIZE
    if size % PAGE_SIZE == 0:
        return size
    return size + (PAGE_SIZE - (size % PAGE_SIZE))


class FirewallDataStructure:
    def __init__(
        self,
        *,
        size: int = 0,
        entry_size: int = 0,
        capacity: int = 1,
        size_formula: Optional[Callable[[FirewallDataStructure], int]] = None,
        elf_name: Optional[str] = None,
        c_name: Optional[str] = None,
    ) -> None:
        self.size = size
        self.entry_size = entry_size
        self.capacity = capacity
        self.size_formula = size_formula or _default_structure_size_formula
        self.elf_name = elf_name
        self.c_name = c_name

        if not size and (entry_size and capacity):
            self.size = self.size_formula(self)

        if not self.size and not (elf_name and c_name):
            raise Exception(
                "FirewallDataStructure: Structure of size 0 created with invalid .elf extraction parameters!"
            )

    def calculate_size(self) -> None:
        if self.size:
            return
        if not self.entry_size:
            raise Exception(
                f"FirewallDataStructure: Entry size of structure with c name {self.c_name} was 0 during size calculation!"
            )
        self.size = self.size_formula(self)
        if not self.size:
            raise Exception(
                f"FirewallDataStructure: Calculated size of structure with c name {self.c_name},"
                f"entry size {self.entry_size} and capacity {self.capacity} was 0!"
            )

    def update_size(self) -> None:
        if not self.entry_size:
            raise Exception(
                f"FirewallDataStructure: Entry size of structure with c name {self.c_name} was 0 during size recalculation!"
            )
        self.size = self.size_formula(self)
        if not self.size:
            raise Exception(
                f"FirewallDataStructure: Recalculated size of structure with c name {self.c_name},"
                f"entry size {self.entry_size} and capacity {self.capacity} was 0!"
            )


def _default_structure_size_formula(structure: FirewallDataStructure) -> int:
    return structure.entry_size * structure.capacity


def _default_region_size_formula(data_structures: list[FirewallDataStructure]) -> int:
    return sum(structure.size for structure in data_structures)


class FirewallMemoryRegions:
    regions: list[FirewallMemoryRegions] = []

    def __init__(
        self,
        *,
        min_size: int = 0,
        data_structures: Optional[list[FirewallDataStructure]] = None,
        size_formula: Optional[Callable[[list[FirewallDataStructure]], int]] = None,
    ) -> None:
        self.min_size = min_size
        self.data_structures = data_structures if data_structures is not None else []
        self.size_formula = size_formula or _default_region_size_formula

        if not min_size and not self.data_structures:
            raise Exception(
                "FirewallMemoryRegions: Region of size 0 created without internal data structure components"
            )
        FirewallMemoryRegions.regions.append(self)

    def calculate_size(self) -> None:
        if self.min_size:
            return
        self.min_size = self.size_formula(self.data_structures)
        if not self.min_size:
            raise Exception(
                "FirewallMemoryRegions: Calculated minimum size of region with data structure "
                f"list {self.data_structures} was 0!"
            )

    def update_size(self) -> None:
        for structure in self.data_structures:
            structure.update_size()
        self.min_size = self.size_formula(self.data_structures)
        if not self.min_size:
            raise Exception(
                "FirewallMemoryRegions: Recalculated minimum size of region with data structure "
                f"list {self.data_structures} was 0!"
            )

    @property
    def region_size(self) -> int:
        if not self.min_size:
            return 0
        return round_up_to_page(self.min_size)


def resolve_region_sizes(dwarfdump_path: str = "llvm-dwarfdump") -> None:
    for region in FirewallMemoryRegions.regions:
        if region.region_size:
            continue

        for structure in region.data_structures:
            if structure.size:
                continue
            try:
                if not structure.elf_name or not structure.c_name:
                    raise Exception(
                        "Missing elf_name/c_name for dynamic structure sizing"
                    )
                if not path.exists(structure.elf_name):
                    raise Exception(
                        f"ERROR: ELF name '{structure.elf_name}' does not exist"
                    )

                output = subprocess.run(
                    [dwarfdump_path, structure.elf_name],
                    capture_output=True,
                    text=True,
                    check=True,
                )
                dwarfdump = output.stdout.split()
                for i, token in enumerate(dwarfdump):
                    if token != "DW_TAG_structure_type":
                        continue
                    if i + 4 >= len(dwarfdump):
                        continue
                    if dwarfdump[i + 2] != f'("{structure.c_name}")':
                        continue
                    if dwarfdump[i + 3] != "DW_AT_byte_size":
                        continue
                    size_fmt = dwarfdump[i + 4].strip("(").strip(")")
                    structure.entry_size = int(size_fmt, base=16)
                    break
            except Exception as e:
                raise Exception(
                    f"Error calculating {structure.c_name} size using llvm-dwarf dump on {structure.elf_name}): {e}"
                )
            structure.calculate_size()
        region.calculate_size()
