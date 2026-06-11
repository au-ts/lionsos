# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
from dataclasses import dataclass
from typing import List
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from importlib.metadata import version
from board import BOARDS

# assert version("sdfgen").split(".")[1] == "28", "Unexpected sdfgen version"

ProtectionDomain = SystemDescription.ProtectionDomain
ProtectionDomain.PRIORITY_MAX = 254

MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

def enableBacktracing(array_of_pds_or_single_pd, show_backtrace_func_list_addr = 0xb00000):
    """
    Wrap an array or single pd as children into a backtracer parent,
    capable of catching faults and then forcing prints of the backtrace
    Remember to compile each of the children with "backtrace.o"
    """
    backtracer = ProtectionDomain("backtracer", "backtracer.elf", priority=ProtectionDomain.PRIORITY_MAX, stack_size=0x10000);
    if isinstance(array_of_pds_or_single_pd, list):
        for child_pd in array_of_pds_or_single_pd:
            backtracer.add_child_pd(child_pd)
    else:
        backtracer.add_child_pd(array_of_pds_or_single_pd)

    # Create a memory region at the predefined address, as an array
    func_list_mr = MemoryRegion(sdf, "backtracerFunctions", prefill_path="test.txt")
    sdf.add_mr(func_list_mr)
    func_list_map = Map(func_list_mr, vaddr=0x20000, perms="r", setvar_vaddr="backtraceFunctions")
    backtracer.add_map(func_list_map)
    return backtracer

def generate(sdf_path: str, output_dir: str):
    faulter_pd = ProtectionDomain("faulter", "faulter.elf", priority=1, stack_size=0x100000)
    backtracer = enableBacktracing(faulter_pd)
    sdf.add_pd(backtracer)

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
