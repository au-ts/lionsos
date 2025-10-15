# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import struct
from random import randint
from dataclasses import dataclass
from typing import List, Tuple
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from importlib.metadata import version

#assert version('sdfgen').split(".")[1] == "26", "Unexpected sdfgen version"

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel


@dataclass
class Board:
    name: str
    arch: SystemDescription.Arch
    paddr_top: int
    serial: str
    timer: str
    blk: str
    blk_partition: int


BOARDS: List[Board] = [
    Board(
        name="maaxboard",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x7_0000_000,
        serial="soc@0/bus@30800000/serial@30860000",
        timer="soc@0/bus@30000000/timer@302d0000",
        blk="soc@0/bus@30800000/mmc@30b40000",
        blk_partition=1,
    ),
]


def generate(sdf_path: str, output_dir: str, dtb: DeviceTree):
    serial_node = dtb.node(board.serial)
    assert serial_node is not None
    blk_node = dtb.node(board.blk)
    assert blk_node is not None
    timer_node = dtb.node(board.timer)
    assert timer_node is not None

    timer_driver = ProtectionDomain(
        "timer_driver", "timer_driver.elf", priority=254)
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    serial_driver = ProtectionDomain(
        "serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = ProtectionDomain(
        "serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_virt_rx = ProtectionDomain(
        "serial_virt_rx", "serial_virt_rx.elf", priority=99)
    serial_system = Sddf.Serial(
        sdf, serial_node, serial_driver, serial_virt_tx, virt_rx=serial_virt_rx)

    blk_driver = ProtectionDomain("blk_driver", "blk_driver.elf", priority=200)
    blk_virt = ProtectionDomain(
        "blk_virt", "blk_virt.elf", priority=199, stack_size=0x2000)
    blk_system = Sddf.Blk(sdf, blk_node, blk_driver, blk_virt)

    doom = ProtectionDomain("doom", "doom.elf", priority=1, stack_size=0x10000)

    serial_system.add_client(doom)
    timer_system.add_client(doom)

    fatfs = ProtectionDomain("fatfs", "fat.elf", priority=96)

    fs = LionsOs.FileSystem.Fat(
        sdf,
        fatfs,
        doom,
        blk=blk_system,
        partition=board.blk_partition
    )

    if board.name == "maaxboard":
        timer_system.add_client(blk_driver)

    dcss_shared_data_mr = MemoryRegion(sdf, "dcss_shared_data", 0x1000)
    video_dma_pool_mr = MemoryRegion(
        sdf, "video_dma_pool", 0x4000000, paddr=0x5000_0000)

    dcss_mr = MemoryRegion(sdf, "dcss", 0x2d000, paddr=0x32e0_0000)
    dcss_blk_mr = MemoryRegion(sdf, "dcss_blk", 0x1000, paddr=0x32e2_f000)
    gpc_mr = MemoryRegion(sdf, "gpc", 0x10000, paddr=0x303a_0000)
    ccm_mr = MemoryRegion(sdf, "ccm", 0x10000, paddr=0x3038_0000)
    hdmi_mr = MemoryRegion(sdf, "hdmi", 0x100000, paddr=0x32c0_0000)

    sdf.add_mr(dcss_shared_data_mr)
    sdf.add_mr(video_dma_pool_mr)

    sdf.add_mr(dcss_mr)
    sdf.add_mr(dcss_blk_mr)
    sdf.add_mr(gpc_mr)
    sdf.add_mr(ccm_mr)
    sdf.add_mr(hdmi_mr)

    dcss = ProtectionDomain("dcss", "dcss.elf", priority=50)
    dcss.add_map(Map(dcss_shared_data_mr, 0x6000_0000, "rw", cached=False))
    dcss.add_map(Map(video_dma_pool_mr, 0x5000_0000, "rw", cached=False))
    dcss.add_map(Map(dcss_mr, 0x32e0_0000, "rw", cached=False))
    dcss.add_map(Map(dcss_blk_mr, 0x32e2_f000, "rw", cached=False))
    dcss.add_map(Map(gpc_mr, 0x303a_0000, "rw", cached=False))
    dcss.add_map(Map(ccm_mr, 0x3038_0000, "rw", cached=False))
    dcss.add_map(Map(hdmi_mr, 0x32c0_0000, "rw", cached=False))

    doom.add_map(Map(video_dma_pool_mr, 0x5000_0000, "rw", cached=False))
    doom.add_map(
        Map(dcss_shared_data_mr, 0x6000_0000, "rw", cached=False))

    sdf.add_channel(Channel(doom, dcss, a_id=42, b_id=0, pp_a=True))
    sdf.add_channel(Channel(doom, dcss, a_id=43, b_id=52, pp_a=True))

    pds = [
        serial_driver,
        serial_virt_tx,
        serial_virt_rx,
        doom,
        fatfs,
        timer_driver,
        blk_driver,
        blk_virt,
        dcss
    ]
    for pd in pds:
        sdf.add_pd(pd)

    assert fs.connect()
    assert fs.serialise_config(output_dir)
    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)
    assert blk_system.connect()
    assert blk_system.serialise_config(output_dir)

    with open(f"{output_dir}/{sdf_path}", "w+") as f:
        f.write(sdf.render())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtb", required=True)
    parser.add_argument("--sddf", required=True)
    parser.add_argument("--board", required=True,
                        choices=[b.name for b in BOARDS])
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)

    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb)
