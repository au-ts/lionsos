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
   # ping pong pairs with interesting interwoven priorities.
    controller = ProtectionDomain(sdf, f"controller", "controller.elf", priority=250)
    testpd = ProtectionDomain(sdf, "test", "test.elf", priority=1)
    controllerSystem = pdc.PDControlSystem(sdf, controller)
    controllerSystem.add_client(testpd)
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

