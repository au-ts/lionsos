# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
from dataclasses import dataclass
from typing import List
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from importlib.metadata import version

assert version("sdfgen").split(".")[1] == "28", "Unexpected sdfgen version"

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
    blk: str
    blk_partition: int


BOARDS: List[Board] = [
    Board(
        name="qemu_virt_aarch64",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x6_0000_000,
        serial="pl011@9000000",
        timer="timer",
        ethernet="virtio_mmio@a003c00",
        blk="virtio_mmio@a003e00",
        blk_partition=0,
    ),
    Board(
        name="maaxboard",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x7_0000_000,
        serial="soc@0/bus@30800000/serial@30860000",
        timer="soc@0/bus@30000000/timer@302d0000",
        ethernet="soc@0/bus@30800000/ethernet@30be0000",
        blk="soc@0/bus@30800000/mmc@30b40000",
        blk_partition=3,
    ),
]


def generate(sdf_path: str, output_dir: str, dtb: DeviceTree):
    serial_node = dtb.node(board.serial)
    assert serial_node is not None
    ethernet_node = dtb.node(board.ethernet)
    assert ethernet_node is not None
    blk_node = dtb.node(board.blk)
    assert blk_node is not None
    timer_node = dtb.node(board.timer)
    assert timer_node is not None

    timer_driver = ProtectionDomain("timer_driver", "timer_driver.elf", priority=254)
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    serial_driver = ProtectionDomain("serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = ProtectionDomain(
        "serial_virt_tx", "serial_virt_tx.elf", priority=99
    )
    serial_virt_rx = ProtectionDomain(
        "serial_virt_rx", "serial_virt_rx.elf", priority=99
    )
    serial_system = Sddf.Serial(
        sdf, serial_node, serial_driver, serial_virt_tx, virt_rx=serial_virt_rx
    )

    ethernet_driver = ProtectionDomain(
        "ethernet_driver", "eth_driver.elf", priority=101, budget=100, period=400
    )
    net_virt_tx = ProtectionDomain(
        "net_virt_tx", "network_virt_tx.elf", priority=100, budget=20000
    )
    net_virt_rx = ProtectionDomain("net_virt_rx", "network_virt_rx.elf", priority=99)
    net_system = Sddf.Net(sdf, ethernet_node, ethernet_driver, net_virt_tx, net_virt_rx)

    blk_driver = ProtectionDomain("blk_driver", "blk_driver.elf", priority=200)
    blk_virt = ProtectionDomain(
        "blk_virt", "blk_virt.elf", priority=199, stack_size=0x2000
    )
    blk_system = Sddf.Blk(sdf, blk_node, blk_driver, blk_virt)

    # Test components
    test_core = ProtectionDomain(
        "test_core", "test_core.elf", priority=1, stack_size=0x10000
    )
    test_file = ProtectionDomain(
        "test_file", "test_file.elf", priority=1, stack_size=0x10000
    )
    test_server = ProtectionDomain(
        "test_server", "test_server.elf", priority=2, stack_size=0x10000
    )
    test_client = ProtectionDomain(
        "test_client", "test_client.elf", priority=1, stack_size=0x10000
    )

    # Notification channel
    ch = Channel(test_server, test_client, a_id=0, b_id=0)
    sdf.add_channel(ch)

    # Network copiers for socket tests
    test_server_copier = ProtectionDomain(
        "test_server_copier", "network_copy_test_server.elf", priority=97, budget=20000
    )
    test_client_copier = ProtectionDomain(
        "test_client_copier", "network_copy_test_client.elf", priority=97, budget=20000
    )

    # Connect test_core (no FS, no network)
    serial_system.add_client(test_core)
    timer_system.add_client(test_core)

    # Connect test_file (FS, no network)
    serial_system.add_client(test_file)
    timer_system.add_client(test_file)

    # Connect test_server (no FS, network)
    serial_system.add_client(test_server)
    timer_system.add_client(test_server)
    net_system.add_client_with_copier(test_server, test_server_copier)
    test_server_lwip = Sddf.Lwip(sdf, net_system, test_server)

    # Connect test_client (no FS, network)
    serial_system.add_client(test_client)
    timer_system.add_client(test_client)
    net_system.add_client_with_copier(test_client, test_client_copier)
    test_client_lwip = Sddf.Lwip(sdf, net_system, test_client)

    # Filesystem for test_file
    fatfs = ProtectionDomain("fatfs", "fat.elf", priority=96)
    fs = LionsOs.FileSystem.Fat(
        sdf, fatfs, test_file, blk=blk_system, partition=board.blk_partition
    )

    if board.name == "maaxboard":
        timer_system.add_client(blk_driver)

    pds = [
        serial_driver,
        serial_virt_tx,
        serial_virt_rx,
        ethernet_driver,
        net_virt_tx,
        net_virt_rx,
        test_core,
        test_file,
        test_server,
        test_client,
        test_server_copier,
        test_client_copier,
        fatfs,
        timer_driver,
        blk_driver,
        blk_virt,
    ]
    for pd in pds:
        sdf.add_pd(pd)

    assert fs.connect()
    assert fs.serialise_config(output_dir)
    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert net_system.connect()
    assert net_system.serialise_config(output_dir)
    assert test_server_lwip.connect()
    assert test_server_lwip.serialise_config(output_dir)
    assert test_client_lwip.connect()
    assert test_client_lwip.serialise_config(output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)
    assert blk_system.connect()
    assert blk_system.serialise_config(output_dir)

    with open(f"{output_dir}/{sdf_path}", "w+") as f:
        f.write(sdf.render())


if __name__ == "__main__":
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
