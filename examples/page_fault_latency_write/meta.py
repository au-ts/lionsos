# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import os, sys
import argparse
from typing import List, Optional
from dataclasses import dataclass
from sdfgen import SystemDescription, Sddf, DeviceTree
from importlib.metadata import version

from board import BOARDS

ProtectionDomain = SystemDescription.ProtectionDomain

def generate(
    sdf_file: str,
    output_dir: str,
    dtb: Optional[DeviceTree],
    need_timer: bool,
    nvme: bool,  # hack to select NVMe or Virtio
):
    timer_node = dtb.node(board.timer)
    assert timer_node is not None
    timer_driver = ProtectionDomain("timer_driver", "timer_driver.elf", priority=254)
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    if dtb is not None:
         blk_node = dtb.node(board.blk)
    


    client = ProtectionDomain("example_pd1", "example_pd1.elf", priority=1)
    pager = ProtectionDomain("pager", "pager.elf", priority=198)

    pds = [pager, client, timer_driver]
    pager.add_child_pd(client)

    for pd in pds:
            sdf.add_pd(pd)


    # add my memory regions and other things
    heap1 = SystemDescription.MemoryRegion(sdf, "heap1", 0x80000, backed=False)
    sdf.add_mr(heap1)


    user_heap_map = SystemDescription.Map(heap1, 0x8000000000, "rw")

    # pager.add_map(pager_heap_map)
    client.add_map(user_heap_map)

    exmmc = SystemDescription.Channel(a=client, b=memory_manager, a_id=0, b_id=0, pp_a=True)
    sdf.add_channel(exmmc)


    timer_system.add_client(pager)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)

    with open(f"{output_dir}/{sdf_file}", "w+") as f:
            f.write(sdf.render())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtb", required=False)
    parser.add_argument("--sddf", required=True)
    parser.add_argument("--board", required=True, choices=[b.name for b in BOARDS])
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)
    parser.add_argument("--need_timer", action="store_true", default=False)
    parser.add_argument("--nvme", action="store_true", default=False)
    parser.add_argument("--partition")

    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    dtb = None
    if board.arch != SystemDescription.Arch.X86_64:
        with open(args.dtb, "rb") as f:
            dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb, False, args.nvme)