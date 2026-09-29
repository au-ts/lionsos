# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause

import argparse
from importlib.metadata import version

from board import BOARDS
from sdfgen import DeviceTree, LionsOs, Sddf, SystemDescription

assert version("sdfgen").split(".")[1] == "35", "Unexpected sdfgen version"

ProtectionDomain = SystemDescription.ProtectionDomain


def generate(sdf_path: str, output_dir: str, dtb: DeviceTree):
    serial_node = dtb.node(board.serial)
    blk_node = dtb.node(board.blk)
    assert serial_node is not None
    assert blk_node is not None

    serial_driver = ProtectionDomain("serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_system = Sddf.Serial(sdf, serial_node, serial_driver, serial_virt_tx)

    blk_driver = ProtectionDomain("blk_driver", "blk_driver.elf", priority=200)
    blk_virt = ProtectionDomain("blk_virt", "blk_virt.elf", priority=199, stack_size=0x2000)
    blk_system = Sddf.Blk(sdf, blk_node, blk_driver, blk_virt)

    client0 = ProtectionDomain("client0", "client0.elf", priority=90, stack_size=0x10000)
    client1 = ProtectionDomain("client1", "client1.elf", priority=90, stack_size=0x10000)
    multiplexer = ProtectionDomain("mux0", "fs_multiplexer.elf", priority=95)
    fatfs = ProtectionDomain("fatfs", "fat_mux.elf", priority=96)

    serial_system.add_client(client0)
    serial_system.add_client(client1)
    fs = LionsOs.FileSystem.Fat(
        sdf,
        fatfs,
        [client0, client1],
        blk=blk_system,
        partition=board.partition,
        multiplexer=multiplexer,
    )

    for pd in [
        serial_driver,
        serial_virt_tx,
        client0,
        client1,
        multiplexer,
        fatfs,
        blk_driver,
        blk_virt,
    ]:
        sdf.add_pd(pd)

    assert fs.connect()
    assert fs.serialise_config(output_dir)
    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert blk_system.connect()
    assert blk_system.serialise_config(output_dir)

    with open(f"{output_dir}/{sdf_path}", "w+", encoding="utf-8") as output:
        output.write(sdf.render())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtb", required=True)
    parser.add_argument("--sddf", required=True)
    parser.add_argument("--board", required=True, choices=[b.name for b in BOARDS])
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)
    args = parser.parse_args()

    board = next(item for item in BOARDS if item.name == args.board)
    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)
    with open(args.dtb, "rb") as dtb_file:
        dtb = DeviceTree(dtb_file.read())
    generate(args.sdf, args.output, dtb)
