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

DEFAULT_CHAIN_LENGTH = 1

def generate(sdf_path: str, output_dir: str):
    rr = RRSystem(sdf)

    starter_pd = ProtectionDomain(rr, "starter", "starter.elf", priority=1);
    chainers: List[ProtectionDomain] = []
    chainers.append(starter_pd)
    for i in range(DEFAULT_CHAIN_LENGTH):
        chainers.append(ProtectionDomain(rr, f"chainer_{i}", "chainer.elf", priority=i+2))

    ender_pd = ProtectionDomain(rr, "ender", "ender.elf", priority=DEFAULT_CHAIN_LENGTH + 2)
    chainers.append(ender_pd)

    channels : List[Channel] = []
    for i in range(len(chainers) - 1):
        channels.append(
            Channel(rr,
                Channel.End(pd=chainers[i], can_notify=True, can_pp=True, ch_id=33, setvar_id="next_caller"),
                Channel.End(pd=chainers[i+1], can_notify=False, can_pp=False, ch_id=59, setvar_id="prev_caller"),
            )
        )

    rr.transfer(sdf, output_dir)

    sdf.write_xml_file(f"{output_dir}/{sdf_path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)

    args = parser.parse_args()

    sdf = System(aarch64, paddr_top=0x10000)

    generate(args.sdf, args.output)
