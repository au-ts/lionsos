# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
from typing import List
from acacia.arch import aarch64
from acacia import ProtectionDomain, MemoryRegion, Map, System, Channel, Subsystem
import xml.etree.ElementTree as et
from dataclasses import dataclass
from abc import ABC
from copy import deepcopy
import pathlib

class RRSystem(System):
    """
    A hooked version of system, which should be techinically used like a subsystem.
    Intercepts all channels, and forces them to pass through a central PD.
    """
    def __init__(self, sdf: System):
        self.__dict__.update(deepcopy(sdf.__dict__))

    def _add_pd(self, pd: "ProtectionDomain"):
        self.pds.add(pd)

    def _add_channel(self, channel: "Channel"):
        # intercept the channel.
        self.channels.add(channel)

    def _add_memory_region(self, mr: "MemoryRegion"):
        self.mrs.add(mr)

    def _add_subsystem(self, subsystem: Subsystem):
        self.subsystems.append(subsystem)

    def resolve_subsystems(self):
        super().resolve_subsystems()

    def make_config_structs(self, build_dir: pathlib.Path = pathlib.Path("./")):
        raise RuntimeError("Unsupported action")

    def render(self) -> et.Element:
        raise RuntimeError("Unsupported action")

    def write_xml_file(self, path: pathlib.Path):
        raise RuntimeError("Unsupported action")

    def transfer(self, sdf: System, rrer: ProtectionDomain):
        """
        Transfers the rrsystem into the sdf system, making all endpoints get intercepted by rrer.
        """
        assert self.arch == sdf.arch
        assert rrer.sdf == sdf
        assert not (rrer in self.pds)
        assert rrer in sdf.pds

        if not self.system_assembled:
            self.resolve_subsystems()

        for pd in self.pds:
            pd.sdf = sdf
            sdf._add_pd(pd)
            assert pd in sdf.pds
            assert pd in rrer.sdf.pds
            print(f"{sdf=}")
            rrer.add_child_pd(pd);

        # Now we should keep track of endpoints so we know who to forward to.
        ch_ind = 0
        for channel in self.channels:
            # intercept channels.
            rrer_end_a = Channel.End(pd=rrer, can_notify=True, can_pp=True, ch_id=ch_ind)
            rrer_end_b = Channel.End(pd=rrer, can_notify=True, can_pp=True, ch_id=ch_ind + 1)

            # end_x are correctly updated because python is funny
            # 100% not stable, but it's the best I can do.
            ch_a = deepcopy(channel)
            ch_a.sdf = sdf
            ch_a.end_b = rrer_end_a

            ch_b = deepcopy(channel)
            ch_b.sdf = sdf
            ch_b.end_a = rrer_end_b

            sdf._add_channel(ch_a)
            sdf._add_channel(ch_b)
            ch_ind += 2

        for mr in self.mrs:
            sdf._add_memory_region(mr)


def generate(sdf_path: str, output_dir: str):
    rr = RRSystem(sdf)
    rrer = ProtectionDomain(sdf, "rrer", "rrer.elf", priority=254)

    ping = ProtectionDomain(rr, "ping", "ping.elf", priority=1)
    pong = ProtectionDomain(rr, "pong", "pong.elf", priority=1)

    # pseudo intercept channels
    ch = Channel(rr,
        Channel.End(pd=ping, can_notify=True, can_pp=False, ch_id=0, setvar_id="pongch"),
        Channel.End(pd=pong, can_notify=True, can_pp=False, ch_id=1, setvar_id="pingch")
    )

    rr.transfer(sdf, rrer)
    sdf.write_xml_file(f"{output_dir}/{sdf_path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)

    args = parser.parse_args()

    sdf = System(aarch64, paddr_top=0x10000)

    generate(args.sdf, args.output)
