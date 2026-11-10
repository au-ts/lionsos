# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause

import argparse
from typing import Optional
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from board import BOARDS

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map

SUPPORTED_BOARDS = ("rpi4b_1gb", "maaxboard", "qemu_virt_aarch64")

# Every process the client forks takes a spare connection to each of the client's serial
# and network virtualisers, so this bounds how many it can fork. 9 is every process slot
# the pager has (PAGER_MAX_CLIENTS) besides the client's own.
DEFAULT_FORK_SLOTS = 9


def generate(
    sdf: SystemDescription,
    board,
    sdf_file: str,
    output_dir: str,
    dtb: Optional[DeviceTree],
    ramfs_image: Optional[str] = None,
    ramfs_size: Optional[int] = None,
    net: bool = False,
    fork_slots: int = DEFAULT_FORK_SLOTS,
):
    serial_node = dtb.node(board.serial)
    assert serial_node is not None

    serial_driver = ProtectionDomain(
        "serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = ProtectionDomain(
        "serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_system = Sddf.Serial(sdf, serial_node, serial_driver, serial_virt_tx)
    pager = ProtectionDomain("pager", "pager.elf", priority=198, passive=True)
    pager_system = LionsOs.Pager(sdf, pager, fork_slots=fork_slots)
    client = ProtectionDomain("client", "client.elf", priority=1, backed=False)
    pager_system.add_client(client)
    serial_system.add_client(client)
    pager_system.add_serial(serial_system)

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
    systems = [serial_system]

    if net:
        ethernet_node = dtb.node(board.ethernet)
        assert ethernet_node is not None
        ethernet_driver = ProtectionDomain("ethernet_driver", "eth_driver.elf", priority=110)
        net_virt_tx = ProtectionDomain("net_virt_tx", "network_virt_tx.elf", priority=109)
        net_virt_rx = ProtectionDomain("net_virt_rx", "network_virt_rx.elf", priority=108)
        net_system = Sddf.Net(sdf, ethernet_node, ethernet_driver, net_virt_tx, net_virt_rx)
        client_copier = ProtectionDomain("client_net_copier", "network_copy_client.elf",
                                         priority=107)
        net_system.add_client_with_copier(client, client_copier)
        # A forked process' spare network connection receives through a copier of its own.
        fork_copiers = [
            ProtectionDomain(f"fork{i}_net_copier", f"network_copy_fork{i}.elf", priority=107)
            for i in range(fork_slots)
        ]
        pager_system.add_net(net_system, fork_copiers)
        pds += [ethernet_driver, net_virt_tx, net_virt_rx, client_copier, *fork_copiers]
        systems.append(net_system)

    for pd in pds:
        sdf.add_pd(pd)
    # The pager hands forked processes copies of the connections the others make, so it is
    # connected after them.
    for system in (*systems, pager_system):
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
    parser.add_argument("--net", action="store_true")
    parser.add_argument("--fork-slots", type=int, default=DEFAULT_FORK_SLOTS)

    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    Sddf(args.sddf)

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    generate(sdf, board, args.sdf, args.output, dtb, args.ramfs_image, args.ramfs_size,
             args.net, args.fork_slots)
