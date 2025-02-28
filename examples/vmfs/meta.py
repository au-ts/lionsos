# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
from random import randint
from dataclasses import dataclass
from typing import List
from sdfgen import SystemDescription, Sddf, DeviceTree, Vmm, LionsOs

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel
VirtualMachine = SystemDescription.VirtualMachine

@dataclass
class Board:
    name: str
    arch: SystemDescription.Arch
    paddr_top: int
    serial: str
    guest_serial: str
    timer: str
    ethernet: str
    blk: str
    guest_blk: str
    # Default partition if the user has not specified one
    partition: int
    passthrough: List[str]

BOARDS: List[Board] = [
    # Note with QEMU: if you have >1 virtio device, the order of them in memory will
    #                 correlate to their order in the QEMU run command. This order is 
    #                 by the QEMU command in vmfs.mk.
    Board(
        name="qemu_virt_aarch64",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x6000_0000,
        serial="pl011@9000000",
        guest_serial="virtio_console@0130000",
        timer="timer",
        ethernet="virtio_mmio@a003c00",
        blk="virtio_mmio@a003e00",
        guest_blk="virtio_blk@0150000",
        partition=0,
        passthrough=[]
    ),
    Board(
        name="maaxboard",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x90000000,
        serial="soc@0/bus@30800000/serial@30860000",
        guest_serial="virtio_console@0130000",
        timer="soc@0/bus@30000000/timer@302d0000",
        ethernet="soc@0/bus@30800000/ethernet@30be0000",
        blk="soc@0/bus@30800000/mmc@30b40000",
        guest_blk="virtio_blk@0150000",
        partition=0,
        passthrough=[]
    ),
]

def generate(sdf_file: str, output_dir: str, dtb: DeviceTree, guest_dtb: DeviceTree):
    uart_node = dtb.node(board.serial)
    assert uart_node is not None
    guest_uart_node = guest_dtb.node(board.guest_serial)
    assert guest_uart_node is not None
    timer_node = dtb.node(board.timer)
    assert uart_node is not None
    ethernet_node = dtb.node(board.ethernet)
    assert ethernet_node is not None
    blk_node = dtb.node(board.blk)
    assert blk_node is not None
    guest_blk_node = guest_dtb.node(board.guest_blk)
    assert guest_blk_node is not None

    uart_driver = ProtectionDomain("uart_driver", "uart_driver.elf", priority=100)
    serial_virt_tx = ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_virt_rx = ProtectionDomain("serial_virt_rx", "serial_virt_rx.elf", priority=99)
    serial_system = Sddf.Serial(sdf, uart_node, uart_driver, serial_virt_tx, virt_rx=serial_virt_rx, enable_color=True)

    timer_driver = ProtectionDomain("timer_driver", "timer_driver.elf", priority=150)
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    ethernet_driver = ProtectionDomain("ethernet_driver", "eth_driver.elf", priority=101, budget=100, period=400)
    net_virt_tx = ProtectionDomain("net_virt_tx", "network_virt_tx.elf", priority=109, budget=100, period=500)
    net_virt_rx = ProtectionDomain("net_virt_rx", "network_virt_rx.elf", priority=108, budget=100, period=500)
    net_system = Sddf.Net(sdf, ethernet_node, ethernet_driver, net_virt_tx, net_virt_rx)

    blk_driver = ProtectionDomain("blk_driver", "blk_driver.elf", priority=110)
    blk_virt = ProtectionDomain("blk_virt", "blk_virt.elf", priority=101)
    blk_system = Sddf.Blk(sdf, blk_node, blk_driver, blk_virt)

    fs_vmm = ProtectionDomain("fs_driver_vmm", "fs_driver_vmm.elf", priority=100)
    fs_vm = VirtualMachine("linux_fs_vm", [VirtualMachine.Vcpu(id=0)])
    fs_vm_system = Vmm(sdf, fs_vmm, fs_vm, guest_dtb)
    partition = 0

    for device_dt_path in board.passthrough:
        node = dtb.node(device_dt_path)
        assert node is not None
        fs_vm_system.add_passthrough_device(node)

    micropython = ProtectionDomain("micropython", "micropython.elf", priority=1)
    micropython_net_copier = ProtectionDomain("micropython_net_copier", "network_copy.elf", priority=97, budget=20000)

    fs_system = LionsOs.FileSystem.VmFs(
        sdf,
        fs_vm_system,
        micropython,
        blk_system,
        guest_blk_node,
        partition
    )

    if board.name == "maaxboard":
        # TODO: address dependancy between drivers in sdfgen more generically
        timer_system.add_client(blk_driver)

    serial_system.add_client(micropython)
    fs_vm_system.add_virtio_mmio_console(guest_uart_node, serial_system)
    timer_system.add_client(micropython)
    micropython_mac_addr = f"52:54:01:00:00:{hex(randint(0, 0xfe))[2:]:0>2}"
    net_system.add_client_with_copier(micropython, micropython_net_copier, mac_addr=micropython_mac_addr)

    pds = [
        uart_driver,
        serial_virt_tx,
        serial_virt_rx,
        ethernet_driver,
        net_virt_tx,
        net_virt_rx,
        micropython,
        micropython_net_copier,
        timer_driver,
        blk_driver,
        blk_virt,
        fs_vmm
    ]
    for pd in pds:
        sdf.add_pd(pd)

    assert serial_system.connect()
    assert timer_system.connect()
    assert net_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert timer_system.serialise_config(output_dir)
    assert net_system.serialise_config(output_dir)

    # The order of these three matters!
    assert fs_vm_system.connect()
    assert fs_system.connect()
    assert blk_system.connect()
    assert fs_vm_system.serialise_config(output_dir)
    assert fs_system.serialise_config(output_dir)
    assert blk_system.serialise_config(output_dir)

    with open(f"{output_dir}/{sdf_file}", "w+") as f:
        f.write(sdf.render())

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtb", required=True)
    parser.add_argument("--guest-dtb", required=True)
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

    with open(args.guest_dtb, "rb") as f:
        guest_dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb, guest_dtb)
