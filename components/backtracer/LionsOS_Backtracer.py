# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
from dataclasses import dataclass
from typing import List
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from importlib.metadata import version
from board import BOARDS
from subprocess import run
from copy import deepcopy

# assert version("sdfgen").split(".")[1] == "28", "Unexpected sdfgen version"

ProtectionDomain = SystemDescription.ProtectionDomain
ProtectionDomain.PRIORITY_MAX = 254

MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

def get_architecture_pointer_alignment(arch: SystemDescription.Arch):
    match arch:
        case SystemDescription.Arch.AARCH64 | SystemDescription.Arch.RISCV64 | SystemDescription.Arch.X86_64:
            return 8
        case SystemDescription.Arch.AARCH32 | SystemDescription.Arch.RISCV32 | SystemDescription.Arch.X86:
            return 4
        case _:
            raise Exception(f"Alignment of architecture {arch} is unknown.")

def enable_backtracing(array_of_pds_or_single_pd, show_backtrace_func_list_addr = 0xb00000):
    """
    Wrap an array or single pd as children into a backtracer parent,
    capable of catching faults and then forcing prints of the backtrace
    Remember to compile each of the children with "backtrace.o"
    """
    backtracer = ProtectionDomain("backtracer", "backtracer.elf", priority=ProtectionDomain.PRIORITY_MAX, stack_size=0x10000);
    pd_elf_paths = [];
    if not isinstance(array_of_pds_or_single_pd, list):
        array_of_pds_or_single_pd = [array_of_pds_or_single_pd]

    for i, child_pd in enumerate(array_of_pds_or_single_pd):
        # Also add a channel for allowing a thread to pause completely
        newChannel = Channel(
            child_pd,
            backtracer,
            a_id = 61,
            b_id = i,
            pp_a = True,
            pd_a_setvar_id="channel_to_backtrace"
        )
        sdf.add_channel(newChannel)
        backtracer.add_child_pd(child_pd)
        pd_elf_paths.append(child_pd.program_image)

    # Create a memory region at the predefined address, as an array
    # Extract each of the addresses of the children's show_backtrace function
    pd_show_backtrace_addrs = []
    for elf_path in pd_elf_paths:
        shell_output = run("set -o pipefail && nm faulter.elf | grep \"show_backtrace\" | cut --delimiter=\" \" -f 1",
            capture_output=True, shell=True, text=True)
        if shell_output.returncode != 0:
            raise Exception(f"Failed to get addresses of 'show_backtrace' for file {elf_path}\n" +
                f"stderr: {shell_output.stderr}\n" +
                f"stdout: {shell_output.stdout}\n" +
                f"exit code: {shell_output.returncode}\n")
        show_backtrace_addr = int(shell_output.stdout.strip(), 16);
        print(f"'{elf_path}':show_backtrace @ {hex(show_backtrace_addr)}")
        pd_show_backtrace_addrs.append(show_backtrace_addr)

    # now write a .data file containing the files spaced out by architectures pointer size.
    alignment = getArchitecturePointerAlignment(board.arch)
    print(f"Alignment for architecture {board.arch.name}: {alignment}")
    frame = b""
    for backtrace_addr in pd_show_backtrace_addrs:
        frame += bytes(backtrace_addr.to_bytes(alignment, "little"))
    BACKTRACER_FUNCTION_DATA_PATH = "backtrace_functions.data"
    with open(BACKTRACER_FUNCTION_DATA_PATH, "wb") as dataFile:
        dataFile.write(frame)
    func_list_mr = MemoryRegion(sdf, "backtracerFunctions", prefill_path=BACKTRACER_FUNCTION_DATA_PATH)
    sdf.add_mr(func_list_mr)
    func_list_map = Map(func_list_mr, vaddr=0x20000, perms="r", setvar_vaddr="backtraceFunctions")
    backtracer.add_map(func_list_map)
    return backtracer


