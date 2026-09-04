# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
from typing import List, Dict
from acacia.arch import aarch64
from acacia import ProtectionDomain, MemoryRegion, Map, System, Channel, Subsystem, PageTables, CSpace, Cap
import xml.etree.ElementTree as et
from dataclasses import dataclass, field
from abc import ABC
from copy import deepcopy
from rrer.rrer import RRChild, RRData, RRSystem
import pathlib


def generate(sdf_path: str, output_dir: str):
    rr = RRSystem(sdf)

    ping = ProtectionDomain(rr, "ping", "ping.elf", priority=1)
    pong = ProtectionDomain(rr, "pong", "pong.elf", priority=2)

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

    args = parser.parse_args()

    sdf = System(aarch64, paddr_top=0x10000)

    generate(args.sdf, args.output)
