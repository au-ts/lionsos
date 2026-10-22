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
import pathlib
from random import randint, seed
import pdcontrol as pdc

def generate(sdf_path: str, output_dir: str):
    controller = ProtectionDomain(sdf, f"controller", "controller.elf", priority=250)
    pingpd = ProtectionDomain(sdf, "ping", "ping.elf", priority=1)
    pongpd = ProtectionDomain(sdf, "pong", "pong.elf", priority=1)
    pingpongch = Channel(
        sdf,
        Channel.End(
            pd=pingpd,
            can_notify=True,
            can_pp=False,
            ch_id=3,
            setvar_id="pongch",
        ),
        Channel.End(
            pd=pongpd,
            can_notify=True,
            can_pp=False,
            ch_id=19,
            setvar_id="pingch",
        )
    )
    controllerSystem = pdc.PDControlSystem(sdf, controller)
    controllerSystem.add_client(pingpd)
    controllerSystem.add_client(pongpd)
    # Bug with my branch of acacia.
    controllerSystem.build()

    sdf.write_xml_file(f"{output_dir}/{sdf_path}")
    sdf.make_config_structs()


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

