# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import struct
from random import randint
from dataclasses import dataclass
from typing import List, Tuple, Optional
from sdfgen import SystemDescription, Sddf, Vmm, DeviceTree, LionsOs

ProtectionDomain = SystemDescription.ProtectionDomain
VirtualMachine = SystemDescription.VirtualMachine
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
        # No I2C device on QEMU
        i2c=None,
    ),
    Board(
        name="odroidc4",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x60000000,
        serial="soc/bus@ff800000/serial@3000",
        timer="soc/bus@ffd00000/watchdog@f0d0",
        ethernet="soc/ethernet@ff3f0000",
        i2c="soc/bus@ffd00000/i2c@1d000",
    ),
]


def generate(sdf_file: str, output_dir: str, dtb: DeviceTree):
    serial_node = dtb.node(board.serial)
    assert serial_node is not None
    ethernet_node = dtb.node(board.ethernet)
    assert ethernet_node is not None
    timer_node = dtb.node(board.timer)
    assert timer_node is not None
    if board.i2c:
        i2c_node = dtb.node(board.i2c)
        assert i2c_node is not None

        # TODO: sort out priorities
        i2c_driver = ProtectionDomain("i2c_driver", "i2c_driver.elf", priority=100)
        i2c_virt = ProtectionDomain("i2c_virt", "i2c_virt.elf", priority=2)
        # Right now we do not have separate clk and GPIO drivers and so our I2C driver does manual
        # clk/GPIO setup for I2C.
        clk_mr = MemoryRegion("clk", 0x1000, paddr=0xff63c000)
        gpio_mr = MemoryRegion("gpio", 0x1000, paddr=0xff634000)
        sdf.add_mr(clk_mr)
        sdf.add_mr(gpio_mr)
        i2c_driver.add_map(Map(clk_mr, 0x30_000_000, Map.Perms(r=True, w=True), cached=False))
        i2c_driver.add_map(Map(gpio_mr, 0x30_100_000, Map.Perms(r=True, w=True), cached=False))
        i2c_system = Sddf.I2c(sdf, i2c_node, i2c_driver, i2c_virt)

    timer_driver = ProtectionDomain("timer_driver", "timer_driver.elf", priority=101)
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    uart_driver = ProtectionDomain("uart_driver", "uart_driver.elf", priority=100)
    serial_virt_tx = ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_virt_rx = ProtectionDomain("serial_virt_rx", "serial_virt_rx.elf", priority=99)
    serial_system = Sddf.Serial(sdf, serial_node, uart_driver, serial_virt_tx, virt_rx=serial_virt_rx)

    ethernet_driver = ProtectionDomain("ethernet_driver", "eth_driver.elf", priority=101, budget=100, period=400)
    net_virt_tx = ProtectionDomain("net_virt_tx", "network_virt_tx.elf", priority=100, budget=20000)
    net_virt_rx = ProtectionDomain("net_virt_rx", "network_virt_rx.elf", priority=99)
    net_system = Sddf.Net(sdf, ethernet_node, ethernet_driver, net_virt_tx, net_virt_rx)

    micropython = ProtectionDomain("micropython", "micropython.elf", priority=1, budget=20000)
    micropython_net_copier = ProtectionDomain("micropython_net_copier", "network_copy_micropython.elf", priority=97, budget=20000)

    serial_system.add_client(micropython)
    timer_system.add_client(micropython)
    micropython_mac_addr = f"52:54:01:00:00:{hex(randint(0, 0xfe))[2:]:0>2}"
    net_system.add_client_with_copier(micropython, micropython_net_copier, mac_addr=micropython_mac_addr)

    nfs_net_copier = ProtectionDomain("nfs_net_copier", "network_copy_nfs.elf", priority=97, budget=20000)

    nfs_mac_addr = f"52:54:01:00:00:{hex(randint(0, 0xfe))[2:]:0>2}"
    nfs = ProtectionDomain("nfs", "nfs.elf", priority=96, stack_size=0x10000)

    fs = LionsOs.FileSystem.Nfs(
        sdf,
        nfs,
        micropython,
        net=net_system,
        net_copier=nfs_net_copier,
        mac_addr=nfs_mac_addr,
        serial=serial_system,
        timer=timer_system,
        server=args.nfs_server,
        export_path=args.nfs_dir,
    )

    vmm = ProtectionDomain("framebuffer_vmm", "vmm.elf", priority=1)
    vm = VirtualMachine("linux", [VirtualMachine.Vcpu(id=0)])
    vmm_system = Vmm(sdf, vmm, vm, guest_dtb, one_to_one_ram=True)

    framebuffer = MemoryRegion("framebuffer", 0x2_000_000)
    sdf.add_mr(framebuffer)
    framebuffer_map = Map(framebuffer, 0x30000000, Map.Perms(r=True, w=True))
    micropython.add_map(framebuffer_map)
    vm.add_map(framebuffer_map)

    if board.name == "qemu_virt_aarch64":
        passthrough_irqs = [Irq(35), Irq(36), Irq(37), Irq(38)]
        vmm_system.add_passthrough_device("gic_v2m", dtb.node("intc@8000000/v2m@8020000"))
        # Other pass-through devices
        devices = [
            ("virtio_mmio", 0x3000, 0xa000000),
            ("pcie", 0x1000000, 0x4010000000),
            ("pcie_config", 0x1000000, 0x10000000),
            ("pcie_bus", 0x1000000, 0x8000000000)
        ]
    elif board.name == "odroidc4":
        pass

    for irq in passthrough_irqs:
        vmm_system.add_passthrough_irq(irq)

    for d in devices:
        mr = MemoryRegion(d[0], d[1], paddr=d[2])
        sdf.add_mr(mr)
        vm.add_map(Map(mr, d[2], Map.Perms(r=True, w=True), cached=False))

    sdf.add_channel(Channel(micropython, vmm))

    pds = [
        uart_driver,
        serial_virt_tx,
        serial_virt_rx,
        ethernet_driver,
        net_virt_tx,
        net_virt_rx,
        micropython,
        micropython_net_copier,
        nfs,
        nfs_net_copier,
        timer_driver,
        vmm,
    ]
    if board.i2c:
        pds += [i2c_driver, i2c_virt]
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
    if board.i2c:
        assert i2c_system.connect()
        assert i2c_system.serialise_config(output_dir)
    assert vmm_system.connect()
    assert vmm_system.serialise_config(output_dir)

    with open(f"{output_dir}/{sdf_file}", "w+") as f:
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
    parser.add_argument("--guest-dtb", required=True)

    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    with open(args.guest_dtb, "rb") as f:
        guest_dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb)
