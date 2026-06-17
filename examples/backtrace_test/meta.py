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
import LionsOS_Backtracer

# assert version("sdfgen").split(".")[1] == "28", "Unexpected sdfgen version"

ProtectionDomain = SystemDescription.ProtectionDomain

MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel
board = 0
sdf = 0


def generate(sdf_path: str, output_dir: str):
    domains = [
        ProtectionDomain(f"faulter{i}", "faulter.elf", priority=i, stack_size=0x100000)
        for i in range(5)
    ]
    backtracer = LionsOS_Backtracer.enable_backtracing(sdf, board.arch, domains)
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
