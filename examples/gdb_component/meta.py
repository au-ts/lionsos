# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
from dataclasses import dataclass
from typing import List
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from importlib.metadata import version
from board import Board
from subprocess import run
from copy import deepcopy

# assert version("sdfgen").split(".")[1] == "28", "Unexpected sdfgen version"

ProtectionDomain = SystemDescription.ProtectionDomain
BOARDS = [
    Board(
        name="qemu_virt_aarch64",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x6_0000_000,
        serial="virtio_mmio@a003e00"
    ),
]

MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

def generate(sdf_path: str, output_dir: str, dtb: DeviceTree):
    uart_node = dtb.node(board.serial)

    assert uart_node is not None

    uart_driver = ProtectionDomain("serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_virt_rx = ProtectionDomain("serial_virt_rx", "serial_virt_rx.elf", priority=99)
    serial_system = Sddf.Serial(sdf, uart_node, uart_driver, serial_virt_tx, virt_rx=serial_virt_rx)

    debugger = ProtectionDomain("debugger", "debugger.elf", priority=98, budget=20000, stack_size=0x100000)

    small_mapping_region = MemoryRegion(sdf, "small_region", 0x1000)
    sdf.add_mr(small_mapping_region)
    small_map = Map(small_mapping_region, 0x900000, "rw", setvar_vaddr="small_mapping_mr")
    debugger.add_map(small_map)

    # large_mapping_region = MemoryRegion(sdf, "large_region", 0x200000, page_size=MemoryRegion.PageSize.LargePage)
    large_mapping_region = MemoryRegion(sdf, "large_region", 0x200000)
    sdf.add_mr(large_mapping_region)
    large_map = Map(large_mapping_region, 0xa00000, "rw", setvar_vaddr="large_mapping_mr")
    debugger.add_map(large_map)

    serial_system.add_client(debugger)

    debug_pds = [
        ProtectionDomain(f"faulter{i}", f"faulter.elf", priority=i)
        for i in range(3)
    ]

    debuggee_pts = SystemDescription.PageTables(setvar="table_metadata")
    for i, pd in enumerate(debug_pds):
        temp = debuggee_pts.add_entry(pd.name, index=i)
    debugger.set_page_tables(debuggee_pts)

    for pd in debug_pds:
        debugger.add_child_pd(pd)

    for pd in [debugger, uart_driver, serial_virt_tx, serial_virt_rx]:
        sdf.add_pd(pd)

    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)

    with open(f"{output_dir}/{sdf_path}", "w+") as f:
        f.write(sdf.render())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--dtb', required=True)
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
