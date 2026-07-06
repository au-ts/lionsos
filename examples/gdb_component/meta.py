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
import LionsOS_debugger

BOARDS = [
    Board(
        name="qemu_virt_aarch64_serial",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x6_0000_000,
        serial="virtio_mmio@a003e00"
    ),
    Board(
        name="qemu_virt_aarch64_net",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x6_0000_000,
        serial="pl011@9000000",
        timer="timer",
        ethernet="virtio_mmio@a003e00"
    ),
]

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

def generate(sdf_path: str, output_dir: str, dtb: DeviceTree):
    uart_node = dtb.node(board.serial)
    assert uart_node is not None


    debug_pds = [
        ProtectionDomain(f"faulter{i}", f"faulter.elf", priority=i)
        for i in range(3)
    ]

    if board.name == "qemu_virt_aarch64_serial":
        uart_driver = ProtectionDomain("serial_driver", "serial_driver.elf", priority=100)
        serial_virt_tx = ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99)
        serial_virt_rx = ProtectionDomain("serial_virt_rx", "serial_virt_rx.elf", priority=99)
        serial_system = Sddf.Serial(sdf, uart_node, uart_driver, serial_virt_tx, virt_rx=serial_virt_rx)
        backend = LionsOS_debugger.Debugger.SerialBackend(serial_system)

        debugger = LionsOS_debugger.Debugger(sdf, backend, priority=98, budget=20000)
        debugger.add_debuggees(debug_pds)
        debuggerPd, _ = debugger.finalise()
        for pd in [debuggerPd, uart_driver, serial_virt_tx, serial_virt_rx]:
            sdf.add_pd(pd)

        assert serial_system.connect()
        assert serial_system.serialise_config(output_dir)

    if board.name == "qemu_virt_aarch64_net":
        ethernet_node = dtb.node(board.ethernet)
        assert ethernet_node is not None
        timer_node = dtb.node(board.timer)
        assert uart_node is not None
        timer_driver = ProtectionDomain("timer_driver", "timer_driver.elf", priority=102)
        timer_system = Sddf.Timer(sdf, timer_node, timer_driver)
        ethernet_driver = ProtectionDomain(
            "ethernet_driver", "eth_driver.elf", priority=101, budget=100, period=400
        )
        net_virt_tx = ProtectionDomain("net_virt_tx", "network_virt_tx.elf", priority=100, budget=20000)
        net_virt_rx = ProtectionDomain("net_virt_rx", "network_virt_rx.elf", priority=99)
        net_system = Sddf.Net(sdf, ethernet_node, ethernet_driver, net_virt_tx, net_virt_rx)
        debugger_net_copier = ProtectionDomain(
            "debugger_net_copier", "network_copy.elf", priority=98, budget=20000
        )

        uart_driver = ProtectionDomain("serial_driver", "serial_driver.elf", priority=97)
        serial_virt_tx = ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=96)
        serial_system = Sddf.Serial(sdf, uart_node, uart_driver, serial_virt_tx)

        backend = LionsOS_debugger.Debugger.NetBackend(sdf,
            net_system,
            debugger_net_copier,
            timer_system,
            [serial_system]
        )

        debugger = LionsOS_debugger.Debugger(sdf, backend, priority=95, budget=20000)
        debugger.add_debuggees(debug_pds)

        debuggerPd, lwip_system = debugger.finalise()

        for pd in [
            debuggerPd,
            uart_driver,
            serial_virt_tx,
            ethernet_driver,
            net_virt_tx,
            net_virt_rx,
            debugger_net_copier,
            timer_driver,
        ]:
            sdf.add_pd(pd)

        assert serial_system.connect()
        assert serial_system.serialise_config(output_dir)
        assert net_system.connect()
        assert net_system.serialise_config(output_dir)
        assert timer_system.connect()
        assert timer_system.serialise_config(output_dir)
        assert lwip_system.connect()
        assert lwip_system.serialise_config(output_dir)

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
