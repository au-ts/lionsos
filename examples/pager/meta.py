# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
"""
Metaprogram for the LionsOS demand-paging example.

Describes a system in which a `client` PD is paged by the `pager` component. The
client is an ordinary PD: it is named as the pager's fault client, which is what
causes the kernel to deliver its VM faults to the pager rather than to the system
fault handler, and its libc routes brk/mmap/munmap/fork to the pager over a
protected procedure call.

Everything the pager needs beyond an ordinary PD -- a BootInfo memory region
describing the untypeds left over after system initialisation, scratch memory for
its shadow page tables, and the CNodes it receives runtime caps into -- is created
by LionsOs.Pager.
"""
import argparse
from typing import Optional

from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs

from board import BOARDS

ProtectionDomain = SystemDescription.ProtectionDomain

# The example's filesystem lives on partition 1 of the block device.
# change if necessary.
FS_PARTITION = 1


def generate(
    sdf: SystemDescription,
    board,
    sdf_file: str,
    output_dir: str,
    dtb: Optional[DeviceTree],
):
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

    fatfs = ProtectionDomain("fatfs", "fat.elf", priority=96)

    pager = ProtectionDomain("pager", "pager.elf", priority=198)
    pager_system = LionsOs.Pager(sdf, pager)

    client = ProtectionDomain("client", "client.elf", priority=1)
    # Leaves the client's stack pages unmapped at boot so that they fault in
    # through the pager, and creates the PPC channel its libc allocates over.
    pager_system.add_client(client)

    serial_system.add_client(client)
    timer_system.add_client(client)

    fs = LionsOs.FileSystem.Fat(
        sdf,
        fatfs,
        client,
        blk=blk_system,
        partition=FS_PARTITION,
        # partition=board.partition,
    )

    # These boards' block drivers need a timer to poll their controllers.
    if board.name in ("maaxboard", "rpi4b_1gb"):
        timer_system.add_client(blk_driver)

    pds = [
        serial_virt_rx,
        timer_driver,
        serial_driver,
        serial_virt_tx,
        pager,
        client,
        blk_driver,
        blk_virt,
        fatfs,
    ]
    for pd in pds:
        sdf.add_pd(pd)

    for system in (pager_system, fs, serial_system, timer_system, blk_system):
        assert system.connect()
        assert system.serialise_config(output_dir)

    with open(f"{output_dir}/{sdf_file}", "w+") as f:
        f.write(sdf.render())


if __name__ == "__main__":
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
    # Not dead code: constructing this registers the sDDF source tree globally,
    # which Sddf.Timer/Serial/Blk rely on.
    Sddf(args.sddf)

    dtb = None
    if board.arch != SystemDescription.Arch.X86_64:
        with open(args.dtb, "rb") as f:
            dtb = DeviceTree(f.read())

    generate(sdf, board, args.sdf, args.output, dtb)
