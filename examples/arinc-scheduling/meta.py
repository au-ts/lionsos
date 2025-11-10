# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import struct
from random import randint
from dataclasses import dataclass
from typing import List, Tuple, Optional
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from importlib.metadata import version

assert version('sdfgen').split(".")[1] == "27", "Unexpected sdfgen version"

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Irq = SystemDescription.Irq
Channel = SystemDescription.Channel

@dataclass
class Board:
    name: str
    arch: SystemDescription.Arch
    paddr_top: int
    serial: str
    timer: str
    ethernet: str
    i2c: Optional[str]


BOARDS: List[Board] = [
    Board(
        name="qemu_virt_aarch64",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x6_0000_000,
        serial="pl011@9000000",
        timer="timer",
        ethernet="virtio_mmio@a003e00",
        i2c=None,
    ),
]


def generate(sdf_path: str, output_dir: str, dtb: DeviceTree):
    timer_node = dtb.node(board.timer)
    assert timer_node is not None

    timer_driver = ProtectionDomain("timer_driver", "timer_driver.elf", priority=201)
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    scheduler = ProtectionDomain("scheduler", "scheduler.elf", priority=200)

    # PARTITION PROTECTION DOMAINS
    p1_spd = ProtectionDomain("p1_spd", "p1_spd.elf", priority=150, passive=True)
    p2_spd = ProtectionDomain("p2_spd", "p2_spd.elf", priority=150, passive=True)
    p3_spd = ProtectionDomain("p3_spd", "p3_spd.elf", priority=150, passive=True)

    p1_upd = ProtectionDomain("p1_upd", "p1_upd.elf", priority=140, passive=True)
    p2_upd = ProtectionDomain("p2_upd", "p2_upd.elf", priority=140, passive=True)
    p3_upd = ProtectionDomain("p3_upd", "p3_upd.elf", priority=140, passive=True)


    partition_initial_pds = [
        p1_spd,
        p2_spd,
        p3_spd,
    ]

    partition_user_pds = [
        p1_upd,
        p2_upd,
        p3_upd,
    ]

    pds = [
        timer_driver,
        scheduler,
    ]

    for pd in pds:
        sdf.add_pd(pd)

    for pd in partition_initial_pds:
        scheduler.add_child_pd(pd)

    # @kwinter: For now make these all children of the scheduler.
    # Once microkit supports handing TCBs to different PD's we will
    # make these user pds children of the initial pds
    for pd in partition_user_pds:
        scheduler.add_child_pd(pd)

    # These channels will start at 0 from the schedulers point of view
    for pd in partition_initial_pds:
        pd_channel = Channel(scheduler, pd)
        sdf.add_channel(pd_channel)

    for pd_init, pd_user in zip(partition_initial_pds, partition_user_pds):
        partition_channel = Channel(pd_init, pd_user)
        sdf.add_channel(partition_channel)

    ### Creating memory regions for simple example
    
    mr1 = MemoryRegion(sdf, "top_impl_Instance_p1_t1_write_port_1_Memory_Region", 0x1000)
    sdf.add_mr(mr1)
    mr2 = MemoryRegion(sdf, "top_impl_Instance_p2_t2_write_port_1_Memory_Region", 0x1000)
    sdf.add_mr(mr2)

    p1_map_mr1 = Map(mr1, 0x10_000_000, perms="rw")
    p1_upd.add_map(p1_map_mr1)

    p2_map_mr1 = Map(mr1, 0x10_000_000, perms="r")
    p2_upd.add_map(p2_map_mr1)
    p2_map_mr2 = Map(mr2, 0x10_001_000, perms="rw")
    p2_upd.add_map(p2_map_mr2)

    p3_map_mr2 = Map(mr2, 0x10_000_000, perms="r")
    p3_upd.add_map(p3_map_mr2);

    timer_system.add_client(scheduler)

    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)

    with open(f"{output_dir}/{sdf_path}", "w+") as f:
        f.write(sdf.render())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtb", required=True)
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
