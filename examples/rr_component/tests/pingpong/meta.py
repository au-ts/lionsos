# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
from typing import List, Dict
from acacia.arch import aarch64
from acacia import ProtectionDomain, MemoryRegion, Map, System, Channel, Subsystem, PageTables, CSpace, Cap, DeviceTreeBlob
from acacia_sddf import BOARDS
import xml.etree.ElementTree as et
from dataclasses import dataclass, field
from abc import ABC
from copy import deepcopy
from rrer.rrer import RRChild, RRData, RRSystem
import pathlib
from random import randint, seed

DEFAULT_NUM_PINGPONG_PAIRS = 1

seed(0)

def generate(sdf_path: str, output_dir: str):
    rr = RRSystem(sdf, board)

    for i in range(DEFAULT_NUM_PINGPONG_PAIRS):
        # ping pong pairs with interesting interwoven priorities.
        ping = ProtectionDomain(rr, f"ping_{i}", "ping.elf", priority=randint(1, 5))
        pong = ProtectionDomain(rr, f"pong_{i}", "pong.elf", priority=randint(1, 5))

        # pseudo intercept channels
        ch = Channel(rr,
            Channel.End(pd=ping, can_notify=True, can_pp=False, ch_id=33, setvar_id="pongch"),
            Channel.End(pd=pong, can_notify=True, can_pp=False, ch_id=59, setvar_id="pingch")
        )

    # add the data of the children here.
    rr.transfer(sdf, output_dir)

    sdf.write_xml_file(f"{output_dir}/{sdf_path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)
    parser.add_argument("--dtb", required=True)

    args = parser.parse_args()
    dtb = DeviceTreeBlob(args.dtb)
    board = next(filter(lambda b: b.name == "qemu_virt_aarch64", BOARDS))
    assert board is not None
    sdf = System(aarch64, paddr_top=board.paddr_top, dtb=dtb)

    generate(args.sdf, args.output)
