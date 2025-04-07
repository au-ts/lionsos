# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import struct
from random import randint
from dataclasses import dataclass
from typing import List, Tuple
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs

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
    blk: str
    blk_partition: int


BOARDS: List[Board] = [
    Board(
        name="qemu_virt_aarch64",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x6_0000_000,
        serial="pl011@9000000",
        timer="timer",
        blk="virtio_mmio@a003e00",
        blk_partition=0,
    ),
    Board(
        name="maaxboard",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x7_0000_000,
        serial="soc@0/bus@30800000/serial@30860000",
        timer="soc@0/bus@30000000/timer@302d0000",
        blk="soc@0/bus@30800000/mmc@30b40000",
        blk_partition=3,
    ),
]



"""
Below are classes to serialise into custom configuration for the benchmarking component.
All serialised definitions are little endian and pointers are 64-bit integers.
"""


class BenchmarkIdleConfig:
    def __init__(self, cycle_counters: int, ch_init: int):
        self.cycle_counters = cycle_counters
        self.ch_init = ch_init

    '''
        Matches struct definition:
        {
            void *;
            uint8_t;
        }
    '''
    def serialise(self) -> bytes:
        return struct.pack("<qc", self.cycle_counters, self.ch_init.to_bytes(1, "little"))


class BenchmarkClientConfig:
    def __init__(self, cycle_counters: int, ch_start: int, ch_stop: int):
        self.cycle_counters = cycle_counters
        self.ch_start = ch_start
        self.ch_stop = ch_stop

    '''
        Matches struct definition:
        {
            void *;
            uint8_t;
            uint8_t;
        }
    '''
    def serialise(self) -> bytes:
        return struct.pack("<qBB", self.cycle_counters, self.ch_start, self.ch_stop)


class BenchmarkConfig:
    def __init__(self, ch_start: int, ch_stop: int, ch_init: int, children: List[Tuple[int, str]]):
        self.ch_start = ch_start
        self.ch_stop = ch_stop
        self.ch_init = ch_init
        self.children = children

    def serialise(self) -> bytes:
        child_config_format = "c" * 65
        pack_str = "<BBBB" + child_config_format * 64
        child_bytes = bytearray()
        for child in self.children:
            c_name = child[1].encode("utf-8")
            c_name_padded = c_name.ljust(64, b'\0')
            assert len(c_name_padded) == 64
            child_bytes.extend(c_name_padded)
            child_bytes.extend(child[0].to_bytes(1, "little"))

        child_bytes = child_bytes.ljust(64 * 65, b'\0')

        child_bytes_list = [x.to_bytes(1, "little") for x in child_bytes]

        return struct.pack(
            pack_str, self.ch_start, self.ch_stop,
            self.ch_init, len(self.children), *child_bytes_list
        )



def generate(sdf_path: str, output_dir: str, dtb: DeviceTree):
    serial_node = dtb.node(board.serial)
    assert serial_node is not None
    blk_node = dtb.node(board.blk)
    assert blk_node is not None
    timer_node = dtb.node(board.timer)
    assert timer_node is not None

    timer_driver = ProtectionDomain("timer_driver", "timer_driver.elf", priority=201)
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    serial_driver = ProtectionDomain("serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_virt_rx = ProtectionDomain("serial_virt_rx", "serial_virt_rx.elf", priority=99)
    serial_system = Sddf.Serial(sdf, serial_node, serial_driver, serial_virt_tx, virt_rx=serial_virt_rx)

    blk_driver = ProtectionDomain("blk_driver", "blk_driver.elf", priority=200)
    blk_virt = ProtectionDomain("blk_virt", "blk_virt.elf", priority=199, stack_size=0x2000)
    blk_system = Sddf.Blk(sdf, blk_node, blk_driver, blk_virt)

    fs_client = ProtectionDomain("fs_client", "fs_client.elf", priority=10)

    serial_system.add_client(fs_client)
    timer_system.add_client(fs_client)

    fatfs = ProtectionDomain("fatfs", "fat.elf", priority=96)

    fs = LionsOs.FileSystem.Fat(
        sdf,
        fatfs,
        fs_client,
        blk=blk_system,
        partition=board.blk_partition
    )

    if board.name == "maaxboard":
        timer_system.add_client(blk_driver)

    # Benchmark specific resources
    bench_idle = ProtectionDomain("bench_idle", "idle.elf", priority=1)
    bench = ProtectionDomain("bench", "benchmark.elf", priority=254)
    serial_system.add_client(bench)

    benchmark_pds = [
        serial_driver,
        serial_virt_tx,
        serial_virt_rx,
        fs_client,
        fatfs,
        timer_driver,
        blk_driver,
        blk_virt,
    ]
    pds = [
        bench_idle,
        bench,
    ]

    bench_children = []
    for pd in benchmark_pds:
        child_id = bench.add_child_pd(pd)
        bench_children.append((child_id, pd.name))
    for pd in pds:
        sdf.add_pd(pd)

    # Benchmark START channel
    bench_start_ch = Channel(fs_client, bench)
    sdf.add_channel(bench_start_ch)
    # Benchmark STOP channel
    bench_stop_ch = Channel(fs_client, bench)
    sdf.add_channel(bench_stop_ch)

    bench_idle_ch = Channel(bench_idle, bench)
    sdf.add_channel(bench_idle_ch)

    cycle_counters_mr = MemoryRegion("cycle_counters", 0x1000)
    sdf.add_mr(cycle_counters_mr)

    bench_idle.add_map(Map(cycle_counters_mr, 0x5_000_000, perms="rw"))
    fs_client.add_map(Map(cycle_counters_mr, 0x20_000_000, perms="rw"))
    bench_idle_config = BenchmarkIdleConfig(0x5_000_000, bench_idle_ch.pd_a_id)

    bench_client_config = BenchmarkClientConfig(
        0x20_000_000,
        bench_start_ch.pd_a_id,
        bench_stop_ch.pd_a_id
    )

    benchmark_config = BenchmarkConfig(
        bench_start_ch.pd_b_id,
        bench_stop_ch.pd_b_id,
        bench_idle_ch.pd_b_id,
        bench_children
    )

    assert fs.connect()
    assert fs.serialise_config(output_dir)
    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)
    assert blk_system.connect()
    assert blk_system.serialise_config(output_dir)

    with open(f"{output_dir}/benchmark_config.data", "wb+") as f:
        f.write(benchmark_config.serialise())

    with open(f"{output_dir}/benchmark_idle_config.data", "wb+") as f:
        f.write(bench_idle_config.serialise())

    with open(f"{output_dir}/benchmark_client_config.data", "wb+") as f:
        f.write(bench_client_config.serialise())

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
