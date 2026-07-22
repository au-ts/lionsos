# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
from typing import List
from acacia.arch import aarch64
from acacia import ProtectionDomain, MemoryRegion, Map, System, Channel, Subsystem
import xml.etree.ElementTree as et
from dataclasses import dataclass
from abc import ABC


class RRSession(System):
    """
    TODO: not sure how this would work because we basically need to wrap a sdf file rather than
    a system. Maybe don't use Subsystem and use something else?
    """
    def __init__(self, sdf: System, name: str):
        super().__init__(sdf, name=name, clients_allowed=True)

    def add_client(self, client: "ProtectionDomain"):
        ...

    def connect_clients(self):
        ...


def generate(sdf_path: str, output_dir: str):
    ping = ProtectionDomain(sdf, "ping", "ping.elf", priority=1)
    pong = ProtectionDomain(sdf, "pong", "pong.elf", priority=1)
    rrer = ProtectionDomain(sdf, "rrer", "rrer.elf", priority=254)
    rrer.add_child_pd(ping);
    rrer.add_child_pd(pong);

    small_mapping_region = MemoryRegion(sdf, "small_region", 0x1000)
    small_map = Map(small_mapping_region, 0x900000, "rw", setvar_vaddr="small_mapping_mr")

    large_mapping_region = MemoryRegion(sdf, "large_region", 0x200000)
    large_map = Map(large_mapping_region, 0xa00000, "rw", setvar_vaddr="large_mapping_mr")
    rrer.add_map(small_map)
    rrer.add_map(large_map)


    #
    # pseudo intercept channels
    # ch = Channel(sdf,
    #     Channel.End(pd=ping, can_notify=True, can_pp=False, ch_id=0, setvar_id="pongch"),
    #     Channel.End(pd=pong, can_notify=True, can_pp=False, ch_id=1, setvar_id="pingch")
    # )
    # becomes 4 channels.
    ch = Channel(sdf,
        Channel.End(pd=ping, can_notify=True, can_pp=False, ch_id=0, setvar_id="pongch"),
        Channel.End(pd=rrer, can_notify=True, can_pp=False, ch_id=1, setvar_id="pingch")
    )
    ch = Channel(sdf,
        Channel.End(pd=rrer, can_notify=True, can_pp=False, ch_id=0, setvar_id="pongch"),
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
