# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import os, sys
import argparse
from typing import List, Optional, Tuple, Callable
from dataclasses import dataclass
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from importlib.metadata import version
import json
import subprocess
import shutil
import struct
from board import BOARDS

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel


def generate(
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


    client = ProtectionDomain("client", "client.elf", priority=1, backed = False)
    pager = SystemDescription.ProtectionDomain("pager", "pager.elf", priority=198)
    
    partition =  board.partition
    pager.add_child_pd(client)
    # add my memory regions and other things
    # SystemDescription.CNode()
    #paging on
    # heap1 = SystemDescription.MemoryRegion(sdf, "heap1", 0x271000, backed=False)
    # heap1 = SystemDescription.MemoryRegion(sdf, "heap1", 0x7d0000, backed=False)
    # paging off 
    # remaining_untypeds_mr = SystemDescription.MemoryRegion(sdf, "remaining_untypeds", size=0x2000, prefill_bootinfo="remaining_untypeds")
    pager_memory = SystemDescription.MemoryRegion(sdf, "pager_memory_mr ", 0x2000000)
    pager_bootinfo = SystemDescription.MemoryRegion(sdf, "pager_bootinfo", 0x2000, prefill_bootinfo="post_capdl_untypeds")
    remaining_untypeds = SystemDescription.CNode("remaining_untypeds", True, 9)
    pagers_empty_cnode = SystemDescription.CNode("pagerspace", False, 20)
    pagers_empty_cnode_map = SystemDescription.CapMap(SystemDescription.CapMap.CapType.Cnode, None, pagers_empty_cnode, 2)
    pager_gzp_cnode = SystemDescription.CNode("gzp", False, 20)
    pager_gzp_cnode_map = SystemDescription.CapMap(SystemDescription.CapMap.CapType.Cnode, None, pager_gzp_cnode, 4)
    pager.add_cap_map(pager_gzp_cnode_map)
    sdf.add_cnode(pager_gzp_cnode)
    pager_ips_cnode = SystemDescription.CNode("ips_cnode", False, 20)
    pager_ips_cnode_map = SystemDescription.CapMap(SystemDescription.CapMap.CapType.Cnode, None, pager_ips_cnode, 3)
    pager.add_cap_map(pager_ips_cnode_map)
    sdf.add_cnode(pager_ips_cnode)
    pager_remaining_untypeds = SystemDescription.CapMap(SystemDescription.CapMap.CapType.Cnode, None, remaining_untypeds, 1)
    pager_bootinfo_map = SystemDescription.Map(pager_bootinfo, 0x8002000000, "rw", setvar_vaddr="remaining_untypeds_vaddr")
    pager.add_map(pager_bootinfo_map)
    sdf.add_mr(pager_bootinfo_map)
    
    pager.add_cap_map(pager_remaining_untypeds)
    pager.add_cap_map(pagers_empty_cnode_map)
    # sdf.add_cnode(remaining_untypeds)
    sdf.add_cnode(remaining_untypeds)
    sdf.add_cnode(pagers_empty_cnode)


    sdf.add_mr(pager_memory)
    # sdf.add_mr(remaining_untypeds_mr)
    pager_memory_map = SystemDescription.Map(pager_memory, 0x8000000000, "rw", setvar_vaddr="pager_memory")
    # untypeds_map = SystemDescription.Map(remaining_untypeds, 0x20_000_000, "rw", setvar_vaddr="remaining_untypeds_vaddr")
    pager.add_map(pager_memory_map)
    # pager.add_map(untypeds_map)
    serial_system.add_client(client)
    timer_system.add_client(client)

    fatfs = ProtectionDomain("fatfs", "fat.elf", priority=96)

    fs = LionsOs.FileSystem.Fat(
        sdf,
        fatfs,
        client,
        blk=blk_system,
        # partition=1 # change this if necessary
        partition=partition
    )

    if board.name == "maaxboard":
        timer_system.add_client(blk_driver)
    if board.name == "rpi4b_1gb":
        timer_system.add_client(blk_driver)

    pds = [
        serial_virt_rx,
        timer_driver,
        serial_driver,
        serial_virt_tx,
        pager,
        blk_driver,
        blk_virt,
        fatfs
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
    # parser.add_argument("--smp", required=True)

    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)



    dtb = None
    if board.arch != SystemDescription.Arch.X86_64:
        with open(args.dtb, "rb") as f:
            dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb)