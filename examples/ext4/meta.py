# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import struct
from random import randint
from dataclasses import dataclass
from typing import List, Tuple
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from importlib.metadata import version

assert version('sdfgen').split(".")[1] == "24", "Unexpected sdfgen version"

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

@dataclass
class Board:
    name: str
    arch: SystemDescription.Arch
    paddr_top: int
    blk: str
    blk_partition: int


BOARDS: List[Board] = [
    Board(
        name="qemu_virt_aarch64",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x6_0000_000,
        blk="virtio_mmio@a003e00",
        blk_partition=0,
    ),
]


def generate(sdf_path: str, output_dir: str, dtb: DeviceTree):
    blk_node = dtb.node(board.blk)
    assert blk_node is not None

    blk_driver = ProtectionDomain("blk_driver", "blk_driver.elf", priority=200)
    blk_virt = ProtectionDomain("blk_virt", "blk_virt.elf", priority=199, stack_size=0x2000)
    blk_system = Sddf.Blk(sdf, blk_node, blk_driver, blk_virt)

    ext4 = ProtectionDomain("ext4", "ext4.elf", priority=1)

    blk_system.add_client(ext4, partition=board.blk_partition)

    pds = [
        ext4,
        blk_driver,
        blk_virt,
    ]
    for pd in pds:
        sdf.add_pd(pd)

    assert blk_system.connect()
    assert blk_system.serialise_config(output_dir)

    with open(f"{output_dir}/{sdf_path}", "w+") as f:
        f.write(sdf.render())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtb", required=True)
    parser.add_argument("--sddf", required=True)
    parser.add_argument("--board", required=True, choices=[b.name for b in BOARDS])
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)

    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb)
