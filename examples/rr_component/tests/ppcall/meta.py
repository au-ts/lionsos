# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
from typing import List, Dict
from acacia.arch import aarch64
from acacia import ProtectionDomain, MemoryRegion, Map, System, Channel, Subsystem, PageTables, CSpace, Cap, DeviceTreeBlob
from acacia_sddf import BOARDS
import xml.etree.ElementTree as et
from dataclasses import dataclass, field
from abc import ABC
from copy import deepcopy
from rrer.rrer import RRChild, RRData, RRSystem
import pathlib
from random import randint, seed

DEFAULT_NUM_PINGPONG_PAIRS = 1

seed(0)

def generate(sdf_path: str, output_dir: str):
    rr = RRSystem(sdf, board)

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
    parser.add_argument("--dtb", required=True)

    args = parser.parse_args()
    dtb = DeviceTreeBlob(args.dtb)
    board = next(filter(lambda b: b.name == "qemu_virt_aarch64", BOARDS))
    assert board is not None
    sdf = System(aarch64, paddr_top=board.paddr_top, dtb=dtb)

    generate(args.sdf, args.output)
