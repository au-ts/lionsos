# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
"""
Metaprogram for the LionsOS demand-paging example.

Describes a system in which the `pager` PD is the parent -- and therefore the
fault handler -- of a single `client` PD. The client's stack is left unbacked
so that it faults in on first touch, and its libc routes brk/mmap/munmap/fork
to the pager over a protected procedure call.

The pager is given every untyped region left over after system initialisation
and builds paging structures out of it on demand, so it needs more from the
system description than an ordinary PD does:

  * a BootInfo memory region describing the leftover untypeds,
  * scratch memory to hold its shadow page tables and frame metadata,
  * several CNodes to receive the caps it creates at runtime.
"""
import argparse
from typing import Optional

from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs

from board import BOARDS

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel
CNode = SystemDescription.CNode
CapMap = SystemDescription.CapMap

# PPC channel the client's libc uses for brk/mmap/munmap/fork. Must match
# PAGER_MEM_CH in lionsos/include/lions/posix/pager_mem.h.
PAGER_MEM_CH = 4

# Scratch memory the pager bump-allocates folio metadata and shadow page
# tables from. Reachable in the pager as the `pager_memory` symbol, which
# src/frame_table.c and src/page_table.c carve up between them.
PAGER_MEMORY_SIZE = 0x2000000
PAGER_MEMORY_VADDR = 0x8000000000

# Filled in by the Microkit tool with a capDLBootInfo_t (see include/untyped.h)
# describing the untypeds that survived system initialisation. Reachable in the
# pager as the `remaining_untypeds_vaddr` symbol.
PAGER_BOOTINFO_SIZE = 0x2000
PAGER_BOOTINFO_VADDR = 0x8002000000

# CNodes mapped into the pager's root CSpace, as
# (name, slot, size_bits, post_capdl_untypeds).
#
# The slots are hard-coded on the other side of this interface and must be kept
# in sync with them:
#   * include/pager.h names every slot (UNTYPED_SLOT, FRAME_CNODE, IPS_CNODE,
#     GZP_CNODE, PROCESS_CNODES, ELF_CAPS) and src/pager.c reaches them via
#     microkit_cspace_root_slot_to_cptr().
#   * The Microkit tool fills the CNode named "elf_caps" with the child PDs'
#     ELF frame caps, and places each child's VSpace cap from slot 7 onwards.
PAGER_CNODES = (
    # All untyped memory left after initialisation.
    ("remaining_untypeds", 1, 9, True),
    # Frames the pager retypes to satisfy faults.
    ("pagerspace", 2, 20, False),
    # Intermediate paging structures (PUD/PD/PT).
    ("ips_cnode", 3, 20, False),
    # Copies of the global zero page cap, one per read-only mapping.
    ("gzp", 4, 20, False),
    # Per-process CSpaces created by fork().
    ("process_cnodes", 5, 5, False),
    # Child PDs' ELF frames, populated by the Microkit tool.
    ("elf_caps", 6, 12, False),
)

# The example's filesystem lives on partition 1 of the block device. This is a
# deliberate override of the per-board default in board.partition; change it to
# match whatever image you boot against.
FS_PARTITION = 1


def add_pager(
    sdf: SystemDescription,
    client: ProtectionDomain,
) -> ProtectionDomain:
    """
    Create the pager PD and everything it needs to service `client`'s faults.

    `client` must not have been added to `sdf` by the caller: it becomes a
    child of the pager, which is what causes its faults to be delivered here.
    """
    pager = ProtectionDomain("pager", "pager.elf", priority=198)
    pager.add_child_pd(client)
    sdf.add_channel(Channel(client, pager, a_id=PAGER_MEM_CH, b_id=0, pp_a=True))

    bootinfo = MemoryRegion(
        sdf,
        "pager_bootinfo",
        PAGER_BOOTINFO_SIZE,
        prefill_bootinfo="post_capdl_untypeds",
    )
    sdf.add_mr(bootinfo)
    pager.add_map(
        Map(bootinfo, PAGER_BOOTINFO_VADDR, "rw",
            setvar_vaddr="remaining_untypeds_vaddr")
    )

    memory = MemoryRegion(sdf, "pager_memory", PAGER_MEMORY_SIZE)
    sdf.add_mr(memory)
    pager.add_map(
        Map(memory, PAGER_MEMORY_VADDR, "rw", setvar_vaddr="pager_memory")
    )

    for name, slot, size_bits, post_capdl_untypeds in PAGER_CNODES:
        cnode = CNode(name, post_capdl_untypeds, size_bits)
        sdf.add_cnode(cnode)
        # pd=None because these CNodes are the pager's alone, not shared.
        pager.add_cap_map(CapMap(CapMap.CapType.Cnode, None, cnode, slot))

    return pager


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

    # backed=False leaves the client's stack pages unmapped at boot so that
    # they fault in through the pager.
    client = ProtectionDomain("client", "client.elf", priority=1, backed=False)
    pager = add_pager(sdf, client)

    serial_system.add_client(client)
    timer_system.add_client(client)

    fs = LionsOs.FileSystem.Fat(
        sdf,
        fatfs,
        client,
        blk=blk_system,
        partition=FS_PARTITION,
    )

    # These boards' block drivers need a timer to poll their controllers.
    if board.name in ("maaxboard", "rpi4b_1gb"):
        timer_system.add_client(blk_driver)

    # The client is deliberately absent: it is added as a child of the pager.
    pds = [
        serial_virt_rx,
        timer_driver,
        serial_driver,
        serial_virt_tx,
        pager,
        blk_driver,
        blk_virt,
        fatfs,
    ]
    for pd in pds:
        sdf.add_pd(pd)

    for system in (fs, serial_system, timer_system, blk_system):
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
