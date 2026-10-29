# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause

import argparse
from typing import Optional
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from board import BOARDS

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map

SUPPORTED_BOARDS = ("rpi4b_1gb", "qemu_virt_aarch64")


def generate(
    sdf: SystemDescription,
    board,
    sdf_file: str,
    output_dir: str,
    dtb: Optional[DeviceTree],
    ramfs_image: Optional[str] = None,
    ramfs_size: Optional[int] = None,
):
    serial_node = dtb.node(board.serial)
    assert serial_node is not None

    serial_driver = ProtectionDomain(
        "serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = ProtectionDomain(
        "serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_system = Sddf.Serial(sdf, serial_node, serial_driver, serial_virt_tx)
    pager = ProtectionDomain("pager", "pager.elf", priority=198, passive=True)
    pager_system = LionsOs.Pager(sdf, pager)
    client = ProtectionDomain("client", "client.elf", priority=1)
    pager_system.add_client(client)
    serial_system.add_client(client)

    # The client's FatFs unpacks this cpio image into a FAT volume in place. The
    # makefile only passes --ramfs-image when RAM_FATFS is set in fat_config.h.
    if ramfs_image is not None:
        disk = MemoryRegion(sdf, "ramfs_disk", ramfs_size, prefill_path=ramfs_image)
        sdf.add_mr(disk)
        client.add_map(Map(disk, client.get_map_vaddr(disk), "rw", setvar_vaddr="disk_start_addr",
                           setvar_size="disk_size"))


    pds = [
        serial_driver,
        serial_virt_tx,
        pager,
        client,
    ]
    for pd in pds:
        sdf.add_pd(pd)
    for system in (pager_system, serial_system):
        assert system.connect()
        assert system.serialise_config(output_dir)
    with open(f"{output_dir}/{sdf_file}", "w+") as f:
        f.write(sdf.render())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtb", required=True)
    parser.add_argument("--sddf", required=True)
    parser.add_argument("--board", required=True, choices=SUPPORTED_BOARDS)
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)
    parser.add_argument("--ramfs-image")
    parser.add_argument("--ramfs-size", type=lambda s: int(s, 0))

    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    Sddf(args.sddf)

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    generate(sdf, board, args.sdf, args.output, dtb, args.ramfs_image, args.ramfs_size)
