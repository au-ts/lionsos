# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
from dataclasses import dataclass
from typing import List
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from importlib.metadata import version
from board import BOARDS
from subprocess import run
from copy import deepcopy

# assert version("sdfgen").split(".")[1] == "28", "Unexpected sdfgen version"

ProtectionDomain = SystemDescription.ProtectionDomain

MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

def generate(sdf_path: str, output_dir: str):
    debug_pds = [
        ProtectionDomain(f"faulter{i}", "faulter.elf", priority=i, stack_size=0x100000)
        for i in range(5)
    ]
    debugger = ProtectionDomain("debugger", "debugger.elf", priority=97, budget=20000, stack_size=0x20000)

    large_mapping_region = MemoryRegion(sdf, "large_region", 0x200000)
    sdf.add_mr(large_mapping_region)
    large_map = Map(large_mapping_region, 0xa00000, "rw", setvar_vaddr="large_mapping_mr")
    debugger.add_map(large_map)

    debuggee_pts = SystemDescription.PageTables(setvar="table_metadata")
    for i, pd in enumerate(debug_pds):
        temp = debuggee_pts.add_entry(pd.name, i)
    debugger.set_page_tables(debuggee_pts)

    for pd in debug_pds:
        debugger.add_child_pd(pd)

    sdf.add_pd(debugger)

    with open(f"{output_dir}/{sdf_path}", "w+") as f:
        f.write(sdf.render())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--sddf", required=True)
    parser.add_argument("--board", required=True, choices=[b.name for b in BOARDS])
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)

    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    generate(args.sdf, args.output)
