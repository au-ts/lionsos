# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import struct
from typing import Dict, List, Any, Tuple
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

class Platform:
    def __init__(self, name: str, arch: SystemDescription.Arch, paddr_top: int, serial: str, timer: str, ethernet: str):
        self.name = name
        self.arch = arch
        self.paddr_top = paddr_top
        self.serial = serial
        self.timer = timer
        self.ethernet = ethernet

# TODO: add QEMU virt RISC-V, i.MX8MM-EVK, i.MX8MQ-EVK, i.MX8MP-EVK
PLATFORMS: List[Platform] = [
    Platform("qemu_virt_aarch64", SystemDescription.Arch.AARCH64, 0x6_0000_000, "pl011@9000000", "timer", "virtio_mmio@a003e00"),
    Platform("odroidc4", SystemDescription.Arch.AARCH64, 0x60000000, "soc/bus@ff800000/serial@3000", "soc/bus@ffd00000/watchdog@f0d0", "soc/ethernet@ff3f0000"),
    Platform("maaxboard", SystemDescription.Arch.AARCH64, 0x70000000, "soc@0/bus@30800000/serial@30860000", "soc@0/bus@30000000/timer@302d0000", "soc@0/bus@30800000/ethernet@30be0000"),
]

def generate(sdf_file: str, output_dir: str, dtb: DeviceTree):
    uart_node = dtb.node(platform.serial)
    assert uart_node is not None
    ethernet_node = dtb.node(platform.ethernet)
    assert ethernet_node is not None
    timer_node = dtb.node(platform.timer)
    assert uart_node is not None

    timer_driver = ProtectionDomain("timer_driver", "timer_driver.elf", priority=101)
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    uart_driver = ProtectionDomain("uart_driver", "uart_driver.elf", priority=100)
    serial_virt_tx = ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_system = Sddf.Serial(sdf, uart_node, uart_driver, serial_virt_tx)

    ethernet_driver = ProtectionDomain("ethernet_driver", "eth_driver.elf", priority=101, budget=100, period=400)
    net_virt_tx = ProtectionDomain("net_virt_tx", "network_virt_tx.elf", priority=100, budget=20000)
    net_virt_rx = ProtectionDomain("net_virt_rx", "network_virt_rx.elf", priority=99)
    net_system = Sddf.Network(sdf, ethernet_node, ethernet_driver, net_virt_tx, net_virt_rx)

    micropython = ProtectionDomain("micropython", "micropython.elf", priority=98, budget=20000)
    micropython_net_copier = ProtectionDomain("micropython_net_copier", "network_copy_micropython.elf", priority=97, budget=20000)

    serial_system.add_client(micropython)
    timer_system.add_client(micropython)
    net_system.add_client_with_copier(micropython, micropython_net_copier, mac_addr="0f:1f:2f:3f:4f:5f")

    nfs_net_copier = ProtectionDomain("nfs_net_copier", "network_copy_nfs.elf", priority=97, budget=20000)

    nfs = ProtectionDomain("nfs", "nfs.elf", priority=96)
    fs = LionsOs.FileSystem.Nfs(sdf, nfs, micropython, net_system, nfs_net_copier)

    pds = [
        uart_driver,
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
    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert net_system.connect()
    assert net_system.serialise_config(output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)

    with open(f"{output_dir}/{sdf_file}", "w+") as f:
        f.write(sdf.xml())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtbs", required=True)
    parser.add_argument("--sddf", required=True)
    parser.add_argument("--platform", required=True, choices=[p.name for p in PLATFORMS])
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)

    args = parser.parse_args()

    platform = next(filter(lambda p: p.name == args.platform, PLATFORMS))

    sdf = SystemDescription(platform.arch, platform.paddr_top)
    sddf = Sddf(args.sddf)

    with open(args.dtbs + f"/{platform.name}.dtb", "rb") as f:
        dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb)
