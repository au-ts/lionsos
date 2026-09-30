# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
from typing import List, Dict
from acacia.arch import aarch64
from acacia import ProtectionDomain, MemoryRegion, Map, System, Channel, Subsystem, PageTables, CSpace, Cap
import xml.etree.ElementTree as et
from dataclasses import dataclass, field
from abc import ABC
from copy import deepcopy
from rrer.rrer import RRChild, RRData, RRSystem
from acacia_sddf.board import Board, DriverDouble
import pathlib

qemu_virt_aarch64 = Board(
    name="qemu_virt_aarch64",
    arch=aarch64,
    paddr_top=0x6_0000_000,
    serial=DriverDouble("arm,pl011", "pl011@9000000"),
    timer=DriverDouble("arm,armv8-timer", "timer"),
    blk=DriverDouble("virtio,mmio", "virtio_mmio@a000200"),
    ethernet=DriverDouble("", "virtio_mmio@a000000"),
    i2c=None,
)

def generate(sdf_path: str, output_dir: str):
    rr = RRSystem(sdf, qemu_virt_aarch64)

    caller = ProtectionDomain(rr, "caller", "caller.elf", priority=1)
    callee = ProtectionDomain(rr, "callee", "callee.elf", priority=2)

    # pseudo intercept channels
    ch = Channel(rr,
        Channel.End(pd=caller, can_notify=True, can_pp=True, ch_id=33, setvar_id="calleech"),
        Channel.End(pd=callee, can_notify=False, can_pp=False, ch_id=59, setvar_id="callerch")
    )

    rr.transfer(sdf, output_dir)

    sdf.write_xml_file(f"{output_dir}/{sdf_path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)

    args = parser.parse_args()

    sdf = System(aarch64, paddr_top=qemu_virt_aarch64.paddr_top)

    generate(args.sdf, args.output)
