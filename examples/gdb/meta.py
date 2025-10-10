# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import struct
import random
from dataclasses import dataclass
from typing import List, Tuple
from sdfgen import SystemDescription, Sddf, DeviceTree
from importlib.metadata import version

assert version('sdfgen').split(".")[1] == "24", "Unexpected sdfgen version"

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel


@dataclass
class Board:
    name: str
    arch: SystemDescription.Arch
    paddr_top: int
    serial: str
    timer: str
    ethernet: str


BOARDS: List[Board] = [
    Board(
        name="qemu_virt_aarch64",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x6_0000_000,
        serial="pl011@9000000",
        timer="timer",
        ethernet="virtio_mmio@a003e00"
    ),
    Board(
        name="odroidc2",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x60000000,
        serial="soc/bus@c8100000/serial@4c0",
        timer="soc/bus@c1100000/watchdog@98d0",
        ethernet="soc/ethernet@c9410000"
    ),
    Board(
        name="odroidc4",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x60000000,
        serial="soc/bus@ff800000/serial@3000",
        timer="soc/bus@ffd00000/watchdog@f0d0",
        ethernet="soc/ethernet@ff3f0000"
    ),
    Board(
        name="maaxboard",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x70000000,
        serial="soc@0/bus@30800000/serial@30860000",
        timer="soc@0/bus@30000000/timer@302d0000",
        ethernet="soc@0/bus@30800000/ethernet@30be0000"
    ),
    Board(
        name="imx8mm_evk",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x70000000,
        serial="soc@0/bus@30800000/spba-bus@30800000/serial@30890000",
        timer="soc@0/bus@30000000/timer@302d0000",
        ethernet="soc@0/bus@30800000/ethernet@30be0000"
    ),
    Board(
        name="imx8mp_evk",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x70000000,
        serial="soc@0/bus@30800000/spba-bus@30800000/serial@30890000",
        timer="soc@0/bus@30000000/timer@302d0000",
        ethernet="soc@0/bus@30800000/ethernet@30be0000"
    ),
    Board(
        name="imx8mq_evk",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x70000000,
        serial="soc@0/bus@30800000/serial@30860000",
        timer="soc@0/bus@30000000/timer@302d0000",
        ethernet="soc@0/bus@30800000/ethernet@30be0000"
    ),
]


def generate(sdf_file: str, output_dir: str, dtb: DeviceTree):
    uart_node = dtb.node(board.serial)
    assert uart_node is not None
    ethernet_node = dtb.node(board.ethernet)
    assert ethernet_node is not None
    timer_node = dtb.node(board.timer)
    assert uart_node is not None

    timer_driver = ProtectionDomain("timer_driver", "timer_driver.elf", priority=101)
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    uart_driver = ProtectionDomain("serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_system = Sddf.Serial(sdf, uart_node, uart_driver, serial_virt_tx)

    ethernet_driver = ProtectionDomain(
        "ethernet_driver", "eth_driver.elf", priority=101, budget=100, period=400
    )
    net_virt_tx = ProtectionDomain("net_virt_tx", "network_virt_tx.elf", priority=100, budget=20000)
    net_virt_rx = ProtectionDomain("net_virt_rx", "network_virt_rx.elf", priority=99)
    net_system = Sddf.Net(sdf, ethernet_node, ethernet_driver, net_virt_tx, net_virt_rx)

    debugger = ProtectionDomain("debugger", "debugger.elf", priority=97, budget=20000, stack_size=0x20000, child_pts=True)
    debugger_net_copier = ProtectionDomain(
        "debugger_net_copier", "network_copy.elf", priority=98, budget=20000
    )

    serial_system.add_client(debugger)
    timer_system.add_client(debugger)
    net_system.add_client_with_copier(debugger, debugger_net_copier)

    debugger_lib_sddf_lwip = Sddf.Lwip(sdf, net_system, debugger)

    small_mapping_region = MemoryRegion("small_region", 0x1000)
    sdf.add_mr(small_mapping_region)
    small_map = Map(small_mapping_region, 0x900000, "rw", setvar_vaddr="small_mapping_mr")
    debugger.add_map(small_map)

    large_mapping_region = MemoryRegion("large_region", 0x200000, page_size=MemoryRegion.PageSize.LargePage)
    sdf.add_mr(large_mapping_region)
    large_map = Map(large_mapping_region, 0xa00000, "rw", setvar_vaddr="large_mapping_mr")
    debugger.add_map(large_map)

    ping = ProtectionDomain("ping", "ping.elf", priority=1)
    pong = ProtectionDomain("pong", "pong.elf", priority=1)

    ping_large_page = MemoryRegion("ping_large_page", 0x200000, page_size=MemoryRegion.PageSize.LargePage)
    sdf.add_mr(ping_large_page)
    ping_large_page_map = Map(ping_large_page, 0x800000, "rw", setvar_vaddr="mr")
    ping.add_map(ping_large_page_map)

    debug_pds = [
        ping,
        pong
    ]

    pds = [
        debugger,
        uart_driver,
        serial_virt_tx,
        ethernet_driver,
        net_virt_tx,
        net_virt_rx,
        debugger_net_copier,
        timer_driver,
    ]

    for pd in debug_pds:
        child_id = debugger.add_child_pd(pd)

    for pd in pds:
        sdf.add_pd(pd)

    ping_pong_channel = Channel(ping, pong)
    sdf.add_channel(ping_pong_channel)

    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert net_system.connect()
    assert net_system.serialise_config(output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)
    assert debugger_lib_sddf_lwip.connect()
    assert debugger_lib_sddf_lwip.serialise_config(output_dir)

    with open(f"{output_dir}/{sdf_file}", "w+") as f:
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
