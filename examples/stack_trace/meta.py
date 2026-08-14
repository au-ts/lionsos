# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import struct
from random import randint
from dataclasses import dataclass
from typing import List, Tuple
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from importlib.metadata import version
from board import BOARDS

assert version('sdfgen').split(".")[1] == "29", "Unexpected sdfgen version"

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

reloader_queue_channel = 23

def generate(sdf_path: str, output_dir: str, dtb: DeviceTree):
    serial_node = dtb.node(board.serial)
    assert serial_node is not None
    ethernet_node = dtb.node(board.ethernet)
    assert ethernet_node is not None
    timer_node = dtb.node(board.timer)
    assert timer_node is not None

    timer_driver = ProtectionDomain("timer_driver", "timer_driver.elf", priority=100, stack_size=0x100000) # don't bother the network stack when reloading back up
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    serial_driver = ProtectionDomain("serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_system = Sddf.Serial(sdf, serial_node, serial_driver, serial_virt_tx)

    ethernet_driver = ProtectionDomain("ethernet_driver", "eth_driver.elf", priority=101, budget=100, period=400)
    net_virt_tx = ProtectionDomain("net_virt_tx", "network_virt_tx.elf", priority=100, budget=20000)
    net_virt_rx = ProtectionDomain("net_virt_rx", "network_virt_rx.elf", priority=99)
    net_system = Sddf.Net(sdf, ethernet_node, ethernet_driver, net_virt_tx, net_virt_rx)

    nfs = ProtectionDomain("nfs", "nfs.elf", priority=96, stack_size=0x10000)

    nfs_lib_sddf_lwip = Sddf.Lwip(sdf, net_system, nfs)

    small_mapping_region = MemoryRegion(sdf, "small_region", 0x1000)
    sdf.add_mr(small_mapping_region)
    small_map = Map(small_mapping_region, 0x900000, "rw")

    large_mapping_region = MemoryRegion(sdf, "large_region", 0x200000)
    sdf.add_mr(large_mapping_region)
    large_map = Map(large_mapping_region, 0xa00000, "rw")

    reloader = ProtectionDomain("reloader", "reloader.elf", priority=150, stack_size=0x10000)

    stack_region = MemoryRegion(sdf, "stack_region", 0x10000)
    sdf.add_mr(stack_region)
    stack_map = Map(stack_region, 0x29000000, "rw")
    reloader.add_map(stack_map)
    reloader.add_map(small_map)
    reloader.add_map(large_map)

    faulting_pd = ProtectionDomain("faulting_pd", "faulting_pd.elf", priority=100, stack_size=0x10000)

    pt = SystemDescription.PageTables(setvar="table_metadata")
    reloader.set_page_tables(pt)

    sdf.add_pd(reloader)
    pds = [
        ethernet_driver,
        net_virt_tx,
        net_virt_rx,
        serial_driver,
        serial_virt_tx,
        timer_driver,
        nfs,
        faulting_pd
    ]

    sdf.add_pd(nfs)

    counter = 1
    for pd in pds:
        if pd != nfs:
            print("We have that ", pd, "has counter", counter)
            reloader.add_child_pd(pd, counter)
            pt.add_entry(pd.name, counter)
            sdf.add_channel(Channel(reloader, pd, a_id=counter, b_id=reloader_queue_channel))
            counter += 1

    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert net_system.connect()
    assert net_system.serialise_config(output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)
    assert nfs_lib_sddf_lwip.connect()
    assert nfs_lib_sddf_lwip.serialise_config(output_dir)

    with open(f"{output_dir}/{sdf_path}", "w+") as f:
        f.write(sdf.render())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtb", required=True)
    parser.add_argument("--sddf", required=True)
    parser.add_argument("--board", required=True, choices=[b.name for b in BOARDS])
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)
    parser.add_argument("--nfs-server", required=True)
    parser.add_argument("--nfs-dir", required=True)

    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb)