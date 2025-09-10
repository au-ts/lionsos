# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import struct
from random import randint
from dataclasses import dataclass
from typing import List, Tuple
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from importlib.metadata import version

assert version('sdfgen').split(".")[1] == "26", "Unexpected sdfgen version"

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

@dataclass
class Board:
    name: str
    arch: SystemDescription.Arch
    paddr_top: int
    serial: str | None
    timer: str | None
    ethernet: str | None


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
        name="x86_64_generic",
        arch=SystemDescription.Arch.X86_64,
        paddr_top=0x7ffdf000,
        serial=None,
        timer=None,
        ethernet=None
    ),
]


def generate(sdf_path: str, output_dir: str, dtb: DeviceTree | None):
    serial_node = None
    ethernet_node = None
    timer_node = None
    if dtb is not None:
        serial_node = dtb.node(board.serial)
        assert serial_node is not None
        ethernet_node = dtb.node(board.ethernet)
        assert ethernet_node is not None
        timer_node = dtb.node(board.timer)
        assert timer_node is not None

    timer_driver = ProtectionDomain("timer_driver", "timer_driver.elf", priority=101)
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    if board.arch == SystemDescription.Arch.X86_64:
        hpet_irq = SystemDescription.IrqMsi(board.arch, pci_bus=0, pci_device=0, pci_func=0, vector=0, handle=0, id=0)
        timer_driver.add_irq(hpet_irq)

        hpet_regs = SystemDescription.MemoryRegion(sdf, "hpet_regs", 0x1000, paddr=0xfed00000)
        hpet_regs_map = SystemDescription.Map(hpet_regs, 0x5000_0000, "rw", cached=False)
        timer_driver.add_map(hpet_regs_map)
        sdf.add_mr(hpet_regs)

    serial_driver = ProtectionDomain("serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_system = Sddf.Serial(sdf, serial_node, serial_driver, serial_virt_tx)

    if board.arch == SystemDescription.Arch.X86_64:
        serial_port = SystemDescription.IoPort(SystemDescription.Arch.X86_64, 0x3f8, 8, 0)
        serial_driver.add_ioport(serial_port)

    ethernet_driver = ProtectionDomain("ethernet_driver", "eth_driver.elf", priority=101, budget=100, period=400)
    net_virt_tx = ProtectionDomain("net_virt_tx", "network_virt_tx.elf", priority=100, budget=20000)
    net_virt_rx = ProtectionDomain("net_virt_rx", "network_virt_rx.elf", priority=99)
    net_system = Sddf.Net(sdf, ethernet_node, ethernet_driver, net_virt_tx, net_virt_rx)

    if board.arch == SystemDescription.Arch.X86_64:
        hw_net_rings = SystemDescription.MemoryRegion(sdf, "hw_net_rings", 65536, paddr=0x7a000000)
        sdf.add_mr(hw_net_rings)
        hw_net_rings_map = SystemDescription.Map(hw_net_rings, 0x7000_0000, "rw")
        ethernet_driver.add_map(hw_net_rings_map)

        virtio_net_regs = SystemDescription.MemoryRegion(sdf, "virtio_net_regs", 0x4000, paddr=0xfe000000)
        sdf.add_mr(virtio_net_regs)
        virtio_net_regs_map = SystemDescription.Map(virtio_net_regs, 0x6000_0000, "rw", cached=False)
        ethernet_driver.add_map(virtio_net_regs_map)

        virtio_net_irq = SystemDescription.IrqIoapic(board.arch, ioapic_id=0, pin=11, vector=1, id=16)
        ethernet_driver.add_irq(virtio_net_irq)

        pci_config_address_port = SystemDescription.IoPort(SystemDescription.Arch.X86_64, 0xCF8, 4, 1)
        ethernet_driver.add_ioport(pci_config_address_port)

        pci_config_data_port = SystemDescription.IoPort(SystemDescription.Arch.X86_64, 0xCFC, 4, 2)
        ethernet_driver.add_ioport(pci_config_data_port)

    micropython = ProtectionDomain("micropython", "micropython.elf", priority=1, budget=20000, stack_size=0x10000)
    micropython_net_copier = ProtectionDomain("micropython_net_copier", "network_copy_micropython.elf", priority=97, budget=20000)

    serial_system.add_client(micropython)
    timer_system.add_client(micropython)
    net_system.add_client_with_copier(micropython, micropython_net_copier)
    micropython_lib_sddf_lwip = Sddf.Lwip(sdf, net_system, micropython)

    nfs_net_copier = ProtectionDomain("nfs_net_copier", "network_copy_nfs.elf", priority=97, budget=20000)

    nfs = ProtectionDomain("nfs", "nfs.elf", priority=96, stack_size=0x10000)

    fs = LionsOs.FileSystem.Nfs(
        sdf,
        nfs,
        micropython,
        net=net_system,
        net_copier=nfs_net_copier,
        serial=serial_system,
        timer=timer_system,
        server=args.nfs_server,
        export_path=args.nfs_dir,
    )
    nfs_lib_sddf_lwip = Sddf.Lwip(sdf, net_system, nfs)

    pds = [
        serial_driver,
        serial_virt_tx,
        ethernet_driver,
        net_virt_tx,
        net_virt_rx,
        micropython,
        micropython_net_copier,
        nfs,
        nfs_net_copier,
        timer_driver,
    ]
    for pd in pds:
        sdf.add_pd(pd)

    assert fs.connect()
    assert fs.serialise_config(output_dir)
    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert net_system.connect()
    assert net_system.serialise_config(output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)
    assert micropython_lib_sddf_lwip.connect()
    assert micropython_lib_sddf_lwip.serialise_config(output_dir)
    assert nfs_lib_sddf_lwip.connect()
    assert nfs_lib_sddf_lwip.serialise_config(output_dir)

    with open(f"{output_dir}/{sdf_path}", "w+") as f:
        f.write(sdf.render())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtb", required=False)
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

    dtb = None
    if board.arch == SystemDescription.Arch.AARCH64:
        with open(args.dtb, "rb") as f:
            dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb)
