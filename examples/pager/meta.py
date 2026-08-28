# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import os, sys
import argparse
from typing import List, Optional, Tuple, Callable
from dataclasses import dataclass
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from importlib.metadata import version
import json
import subprocess
import shutil
import struct
from board import BOARDS

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

class BenchmarkIdleConfig:
    def __init__(self, cycle_counters: int, ch_init: int):
        self.cycle_counters = cycle_counters
        self.ch_init = ch_init

    """
        Matches struct definition:
        {
            void *;
            uint8_t;
        }
    """

    def serialise(self) -> bytes:
        return struct.pack(
            "<qc", self.cycle_counters, self.ch_init.to_bytes(1, "little")
        )


class BenchmarkClientConfig:
    def __init__(self, ch_start: int, ch_stop: int, cycle_counters: List[int]):
        self.cycle_counters = cycle_counters
        self.ch_start = ch_start
        self.ch_stop = ch_stop

    """
        Matches struct definition:
        {
            uint8_t;
            uint8_t;
            uint8_t;
            void * [];
        }
    """

    def serialise(self) -> bytes:
        # Padded for 64 bit alignment
        pack_str = "<BBBxxxxx" + "q" * len(self.cycle_counters)
        return struct.pack(
            pack_str,
            self.ch_start,
            self.ch_stop,
            len(self.cycle_counters),
            *self.cycle_counters,
        )


class BenchmarkConfig:
    def __init__(
        self,
        ch_rx_start: int,
        ch_tx_start: int,
        ch_rx_stop: int,
        ch_tx_stop: int,
        ch_init: int,
        core: int,
        last_core: bool,
        children: List[Tuple[int, str]],
    ):
        self.ch_rx_start = ch_rx_start
        self.ch_tx_start = ch_tx_start
        self.ch_rx_stop = ch_rx_stop
        self.ch_tx_stop = ch_tx_stop
        self.ch_init = ch_init
        self.core = core
        self.last_core = last_core
        self.children = children

    """
        Matches struct definition:
        {
            uint8_t;
            uint8_t;
            uint8_t;
            uint8_t;
            uint8_t;
            uint8_t;
            bool;
            uint8_t;
            struct {
                char [64];
                uint8_t;
            } [64];
        }
    """

    def serialise(self) -> bytes:
        child_config_format = "c" * 65
        pack_str = "<BBBBBB?B" + child_config_format * 64
        child_bytes = bytearray()
        for child in self.children:
            c_name = child[1].encode("utf-8")
            c_name_padded = c_name.ljust(64, b"\0")
            assert len(c_name_padded) == 64
            child_bytes.extend(c_name_padded)
            child_bytes.extend(child[0].to_bytes(1, "little"))

        child_bytes = child_bytes.ljust(64 * 65, b"\0")

        child_bytes_list = [x.to_bytes(1, "little") for x in child_bytes]

        return struct.pack(
            pack_str,
            self.ch_rx_start,
            self.ch_tx_start,
            self.ch_rx_stop,
            self.ch_tx_stop,
            self.ch_init,
            self.core,
            self.last_core,
            len(self.children),
            *child_bytes_list,
        )


# Adds ".elf" to elf strings
def copy_elf(source_elf: str, new_elf: str, elf_number=None):
    source_elf += ".elf"
    if elf_number != None:
        new_elf += str(elf_number)
    new_elf += ".elf"
    assert os.path.isfile(source_elf)
    return shutil.copyfile(source_elf, new_elf)


# Assumes elf string has ".elf" suffix, and ".data" to data string
def update_elf_section(
    elf_name: str, section_name: str, data_name: str, data_number=None
):
    assert os.path.isfile(elf_name)
    if data_number != None:
        data_name += str(data_number)
    data_name += ".data"
    assert os.path.isfile(data_name)
    assert (
        subprocess.run(
            [
                obj_copy,
                "--update-section",
                "." + section_name + "=" + data_name,
                elf_name,
            ]
        ).returncode
        == 0
    )

def generate(
    sdf_file: str,
    output_dir: str,
    dtb: Optional[DeviceTree],
    get_core: Callable[[str], int]
):
    serial_node = dtb.node(board.serial)
    assert serial_node is not None
    blk_node = dtb.node(board.blk)
    assert blk_node is not None
    timer_node = dtb.node(board.timer)
    assert timer_node is not None
    
    timer_driver = ProtectionDomain(
        "timer_driver", "timer_driver.elf", priority=254)
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    serial_driver = ProtectionDomain(
        "serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = ProtectionDomain(
        "serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_virt_rx = ProtectionDomain(
        "serial_virt_rx", "serial_virt_rx.elf", priority=99)
    serial_system = Sddf.Serial(
        sdf, serial_node, serial_driver, serial_virt_tx, virt_rx=serial_virt_rx)

    blk_driver = ProtectionDomain("blk_driver", "blk_driver.elf", priority=200)
    blk_virt = ProtectionDomain(
        "blk_virt", "blk_virt.elf", priority=199, stack_size=0x2000)
    blk_system = Sddf.Blk(sdf, blk_node, blk_driver, blk_virt)


    client = ProtectionDomain("client", "client.elf", priority=1, backed = False)
    pager = SystemDescription.ProtectionDomain("pager", "pager.elf", priority=198)
    
    client_pager_bench_cmds = SystemDescription.Channel(client, pager, a_id=0, b_id=0, pp_a=True)
    sdf.add_channel(client_pager_bench_cmds)
    partition =  board.partition
    pager.add_child_pd(client)
    # add my memory regions and other things
    # SystemDescription.CNode()
    #paging on
    # heap1 = SystemDescription.MemoryRegion(sdf, "heap1", 0x271000, backed=False)
    # heap1 = SystemDescription.MemoryRegion(sdf, "heap1", 0x7d0000, backed=False)
    # paging off 
    # remaining_untypeds_mr = SystemDescription.MemoryRegion(sdf, "remaining_untypeds", size=0x2000, prefill_bootinfo="remaining_untypeds")
    pager_memory = SystemDescription.MemoryRegion(sdf, "pager_memory_mr ", 0x2000000)
    pager_bootinfo = SystemDescription.MemoryRegion(sdf, "pager_bootinfo", 0x2000, prefill_bootinfo="post_capdl_untypeds")
    remaining_untypeds = SystemDescription.CNode("remaining_untypeds", True, 9)
    pagers_empty_cnode = SystemDescription.CNode("pagerspace", False, 20)
    pagers_empty_cnode_map = SystemDescription.CapMap(SystemDescription.CapMap.CapType.Cnode, None, pagers_empty_cnode, 2)
    pager_gzp_cnode = SystemDescription.CNode("gzp", False, 20)
    pager_gzp_cnode_map = SystemDescription.CapMap(SystemDescription.CapMap.CapType.Cnode, None, pager_gzp_cnode, 4)
    pager.add_cap_map(pager_gzp_cnode_map)
    sdf.add_cnode(pager_gzp_cnode)
    pager_ips_cnode = SystemDescription.CNode("ips_cnode", False, 20)
    pager_ips_cnode_map = SystemDescription.CapMap(SystemDescription.CapMap.CapType.Cnode, None, pager_ips_cnode, 3)
    pager.add_cap_map(pager_ips_cnode_map)
    sdf.add_cnode(pager_ips_cnode)
    pager_remaining_untypeds = SystemDescription.CapMap(SystemDescription.CapMap.CapType.Cnode, None, remaining_untypeds, 1)
    pager_bootinfo_map = SystemDescription.Map(pager_bootinfo, 0x8002000000, "rw", setvar_vaddr="remaining_untypeds_vaddr")
    pager.add_map(pager_bootinfo_map)
    sdf.add_mr(pager_bootinfo_map)
    
    pager.add_cap_map(pager_remaining_untypeds)
    pager.add_cap_map(pagers_empty_cnode_map)
    # sdf.add_cnode(remaining_untypeds)
    sdf.add_cnode(remaining_untypeds)
    sdf.add_cnode(pagers_empty_cnode)


    sdf.add_mr(pager_memory)
    # sdf.add_mr(remaining_untypeds_mr)
    pager_memory_map = SystemDescription.Map(pager_memory, 0x8000000000, "rw", setvar_vaddr="pager_memory")
    # untypeds_map = SystemDescription.Map(remaining_untypeds, 0x20_000_000, "rw", setvar_vaddr="remaining_untypeds_vaddr")
    pager.add_map(pager_memory_map)
    # pager.add_map(untypeds_map)
    serial_system.add_client(client)
    timer_system.add_client(client)

    fatfs = ProtectionDomain("fatfs", "fat.elf", priority=96)

    fs = LionsOs.FileSystem.Fat(
        sdf,
        fatfs,
        client,
        blk=blk_system,
        # partition=1 # change this if necessary
        partition=partition
    )

    if board.name == "maaxboard":
        timer_system.add_client(blk_driver)
    if board.name == "rpi4b_1gb":
        timer_system.add_client(blk_driver)

    child_pds = [
        timer_driver,
        serial_driver,
        serial_virt_tx,
        pager,
        blk_driver,
        blk_virt,
        fatfs
    ]
    pds_per_core = {
    }

    for pd in child_pds:
        try:
            core = get_core(pd.name)
        except:
            raise ValueError(
                f"PD {pd.name} is missing from your core allocation configuration file!"
            )
        if core in pds_per_core:
            pds_per_core[core].append(pd)
        else:
            pds_per_core[core] = [pd]
    num_cores = len(pds_per_core)

    # Allocate benchmarking resources
    core_objs = [{} for _ in range(num_cores)]
    for i in range(num_cores):
        core = sorted(pds_per_core)[i]
        core_objs[i]["core"] = core

        # Create benchmark and idle PDs for each active core
        core_objs[i]["idle_elf"] = copy_elf("idle", "idle", core)
        core_objs[i]["idle_pd"] = ProtectionDomain(
            f"bench_idle{core}", core_objs[i]["idle_elf"], priority=1, cpu=core
        )
        sdf.add_pd(core_objs[i]["idle_pd"])

        core_objs[i]["bench_elf"] = copy_elf("benchmark", "benchmark", core)
        core_objs[i]["bench_pd"] = ProtectionDomain(
            f"bench{core}", core_objs[i]["bench_elf"], priority=254, cpu=core
        )
        sdf.add_pd(core_objs[i]["bench_pd"])

        # Benchmark PD requires serial output
        serial_system.add_client(core_objs[i]["bench_pd"])

        # Create formatted list of children for benchmark PD
        core_objs[i]["children"] = []
        for pd in pds_per_core[core]:
            child_id = core_objs[i]["bench_pd"].add_child_pd(pd)
            core_objs[i]["children"].append((child_id, pd.name))

        # Create benchmark to idle init channel
        core_objs[i]["init_ch"] = Channel(
            core_objs[i]["idle_pd"], core_objs[i]["bench_pd"]
        )
        sdf.add_channel(core_objs[i]["init_ch"])

        # Create benchmarking start and stop channels
        if i == 0:
            # First active core is notified by benchmarking client
            core_objs[i]["start_ch"] = Channel(pager, core_objs[i]["bench_pd"])
            core_objs[i]["stop_ch"] = Channel(pager, core_objs[i]["bench_pd"])
        else:
            # Other cores are notified by benchmark PD on previous core
            core_objs[i]["start_ch"] = Channel(
                core_objs[i - 1]["bench_pd"], core_objs[i]["bench_pd"]
            )
            core_objs[i]["stop_ch"] = Channel(
                core_objs[i - 1]["bench_pd"], core_objs[i]["bench_pd"]
            )

        sdf.add_channel(core_objs[i]["start_ch"])
        sdf.add_channel(core_objs[i]["stop_ch"])

        # Add cycle counter memory region for idle to share counts with benchmarking client
        cycle_counters_mr = MemoryRegion(sdf, f"cycle_counters{core}", 0x1000)
        sdf.add_mr(cycle_counters_mr)
        core_objs[i]["idle_pd"].add_map(Map(cycle_counters_mr, 0x5_000_000, perms="rw"))
        client.add_map(Map(cycle_counters_mr, 0x20_000_000 + 0x1000 * i, perms="r"))

        # Create configuration structures to be serialised
        core_objs[i]["idle_config"] = BenchmarkIdleConfig(
            0x5_000_000, core_objs[i]["init_ch"].pd_a_id
        )
        if i == 0:
            # We first create a config for the benchmarking client
            bench_client_config = BenchmarkClientConfig(
                core_objs[i]["start_ch"].pd_a_id,
                core_objs[i]["stop_ch"].pd_a_id,
                list(((0x20_000_000 + 0x1000 * i) for i in range(num_cores))),
            )
        else:
            # Then we create the config for the benchmark PD on the previous core
            core_objs[i - 1]["bench_config"] = BenchmarkConfig(
                core_objs[i - 1]["start_ch"].pd_b_id,
                core_objs[i]["start_ch"].pd_a_id,
                core_objs[i - 1]["stop_ch"].pd_b_id,
                core_objs[i]["stop_ch"].pd_a_id,
                core_objs[i - 1]["init_ch"].pd_b_id,
                core_objs[i - 1]["core"],
                False,
                core_objs[i - 1]["children"],
            )

    # Finally create the last benchmark PD config
    core_objs[num_cores - 1]["bench_config"] = BenchmarkConfig(
        core_objs[num_cores - 1]["start_ch"].pd_b_id,
        0,
        core_objs[num_cores - 1]["stop_ch"].pd_b_id,
        0,
        core_objs[num_cores - 1]["init_ch"].pd_b_id,
        core_objs[num_cores - 1]["core"],
        True,
        core_objs[num_cores - 1]["children"],
    )

    pds = [
        serial_virt_rx,
    ]

    
    for pd in pds:
        sdf.add_pd(pd)

    assert fs.connect()
    assert fs.serialise_config(output_dir)
    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)
    assert blk_system.connect()
    assert blk_system.serialise_config(output_dir)

    with open(f"{output_dir}/benchmark_client_config.data", "wb+") as f:
        f.write(bench_client_config.serialise())
    update_elf_section(
        "pager.elf", "benchmark_client_config", "benchmark_client_config"
    )

    for i in range(num_cores):
        core = core_objs[i]["core"]
        update_elf_section(
            core_objs[i]["bench_elf"],
            "serial_client_config",
            "serial_client_bench",
            core,
        )

        with open(f"{output_dir}/benchmark_config{core}.data", "wb+") as f:
            f.write(core_objs[i]["bench_config"].serialise())
        update_elf_section(
            core_objs[i]["bench_elf"], "benchmark_config", "benchmark_config", core
        )

        with open(f"{output_dir}/benchmark_idle_config{core}.data", "wb+") as f:
            f.write(core_objs[i]["idle_config"].serialise())
        update_elf_section(
            core_objs[i]["idle_elf"], "benchmark_config", "benchmark_idle_config", core
        )

    with open(f"{output_dir}/{sdf_file}", "w+") as f:
            f.write(sdf.render())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtb", required=True)
    parser.add_argument("--sddf", required=True)
    parser.add_argument("--board", required=True,
                        choices=[b.name for b in BOARDS])
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument("--smp", required=True)

    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    global obj_copy
    obj_copy = args.objcopy

    with open(args.smp, "r") as core_alloc:
        core_dict = json.load(core_alloc)
    get_core = lambda name: core_dict[name]

    dtb = None
    if board.arch != SystemDescription.Arch.X86_64:
        with open(args.dtb, "rb") as f:
            dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb, get_core)