# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
from typing import List
from acacia.arch import aarch64
from acacia import ProtectionDomain, MemoryRegion, Map, System, Channel
import xml.etree.ElementTree as et

def generate(sdf_path: str, output_dir: str):
    ping = ProtectionDomain(sdf, "ping", "ping.elf", priority=1)
    pong = ProtectionDomain(sdf, "pong", "pong.elf", priority=1)
    ch = Channel(sdf,
        Channel.End(pd=ping, can_notify=True, can_pp=False, ch_id=0, setvar_id="pongch"),
        Channel.End(pd=pong, can_notify=True, can_pp=False, ch_id=1, setvar_id="pingch")
    )
    sdf.write_xml_file(f"{output_dir}/{sdf_path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)

    args = parser.parse_args()

    sdf = System(aarch64, paddr_top=0x10000)

    generate(args.sdf, args.output)
