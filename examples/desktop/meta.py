# Copyright 2026, LionsOS Contributors
# SPDX-License-Identifier: BSD-2-Clause
#
# Generates the system description for the desktop example.
#
#   notes, sketch, clock,  <->  compositor  <->  gpu_virt   <->  gpu_driver (virtIO GPU)
#   calculator, widgets              <---  input_virt <---  input_keyboard, input_tablet
#   clock  -------------------------------->  timer_driver
#
# sdfgen 0.35 has no GPU or input device class, so those drivers, their
# virtualisers and their shared regions are described by hand here. The GPU
# part mirrors sDDF's examples/gpu. The timer uses sdfgen's Sddf.Timer as in
# other examples.
import argparse
import re
import struct
from typing import List, Tuple
from sdfgen import SystemDescription, Sddf, DeviceTree
from importlib.metadata import version
from board import BOARDS

assert version('sdfgen').split(".")[1] == "35", "Unexpected sdfgen version"

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel
Irq = SystemDescription.IrqConventional

# Must match include/gpu_config.h
GPU_EVENTS_REGION_SIZE = 0x1000
GPU_QUEUE_REGION_SIZE = 0x200_000
GPU_DRIVER_DATA_REGION_SIZE = 0x1000
GPU_CLIENT_DATA_REGION_SIZE = 0x400_000
GPU_VIRTIO_METADATA_REGION_SIZE = 0x200_000
GPU_VIRTIO_DATA_REGION_SIZE = 0x200_000

# Must match include/gui_config.h. Apps are listed in slot order.
GUI_APPS = ["notes", "sketch", "clock", "calculator", "widgets"]
GUI_SURFACE_REGION_SIZE = 0x100_000
GUI_STATE_REGION_SIZE = 0x1000
GUI_EVENTS_REGION_SIZE = 0x1000
GUI_APP_CH_BASE = 10

# Must match include/input_config.h
INPUT_QUEUE_REGION_SIZE = 0x1000
INPUT_QUEUE_CAPACITY = (INPUT_QUEUE_REGION_SIZE - 16) // 8
INPUT_VIRTIO_DMA_SIZE = 0x2000
INPUT_DRIVER_CONFIG_MAGIC = 0x4c494e50
INPUT_DEVICES = ["keyboard", "tablet"]


class VirtioMmioLayout:
    def __init__(self, page: int, devices: dict):
        # Physical address of the 4K page holding the virtIO MMIO transports
        self.page = page
        # name -> (offset of the transport within the page, IRQ)
        self.devices = devices


# QEMU's virt machine has 32 virtIO MMIO transports of 0x200 bytes from
# 0xa000000, with transport n using IRQ 48 + n. The `qemu` target in
# desktop.mk puts each device on a fixed transport (bus=virtio-mmio-bus.n):
# the GPU on 31 (the offset the sDDF GPU driver expects), the keyboard on 30
# and the tablet on 29. All three share the page at 0xa003000.
VIRTIO_MMIO = {
    "qemu_virt_aarch64": VirtioMmioLayout(0xa003000, {
        "gpu": (0xe00, 79),
        "keyboard": (0xc00, 78),
        "tablet": (0xa00, 77),
    }),
}


def add_gpu_queues(sdf: SystemDescription, prefix: str) -> List[MemoryRegion]:
    mrs = [
        MemoryRegion(sdf, f"{prefix}_events", GPU_EVENTS_REGION_SIZE),
        MemoryRegion(sdf, f"{prefix}_req_queue", GPU_QUEUE_REGION_SIZE),
        MemoryRegion(sdf, f"{prefix}_resp_queue", GPU_QUEUE_REGION_SIZE),
    ]
    for mr in mrs:
        sdf.add_mr(mr)
    return mrs


def map_gpu_queues(pd: ProtectionDomain, mrs: List[MemoryRegion], vaddr: int, names: Tuple[str, str, str]):
    for mr, name in zip(mrs, names):
        pd.add_map(Map(mr, vaddr, "rw", cached=False, setvar_vaddr=name))
        vaddr += GPU_QUEUE_REGION_SIZE


def add_region_paddr_setvars(xml: str, pd_name: str, setvars: List[Tuple[str, str]]) -> str:
    """
    The GPU driver needs the physical addresses of some regions, which sDDF
    device classes get through sdfgen-generated device resources. sdfgen 0.35
    cannot express that for a hand-described PD, so insert Microkit
    <setvar region_paddr=.../> elements into the rendered description.
    """
    pattern = re.compile(rf'(<protection_domain name="{pd_name}".*?)(\n\s*</protection_domain>)', re.DOTALL)
    lines = "".join(f'\n        <setvar symbol="{symbol}" region_paddr="{mr}" />' for symbol, mr in setvars)
    xml, n = pattern.subn(lambda m: m.group(1) + lines + m.group(2), xml, count=1)
    assert n == 1, f"protection domain {pd_name} not found"
    return xml


def generate(sdf_path: str, output_dir: str, dtb: DeviceTree):
    timer_node = dtb.node(board.timer)
    assert timer_node is not None
    mmio = VIRTIO_MMIO[board.name]

    timer_driver = ProtectionDomain("timer_driver", "timer_driver.elf", priority=254)
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    gpu_driver = ProtectionDomain("gpu_driver", "gpu_driver.elf", priority=254, stack_size=0x10000)
    gpu_virt = ProtectionDomain("gpu_virt", "gpu_virt.elf", priority=99, stack_size=0x10000)
    compositor = ProtectionDomain("compositor", "compositor.elf", priority=10, stack_size=0x10000)

    # Device and DMA regions of the driver. The DMA regions are physical so
    # that they are contiguous and their addresses can be handed to the device.
    virtio_regs = MemoryRegion(sdf, "virtio_mmio", 0x1000, paddr=mmio.page)
    virtio_metadata = MemoryRegion(sdf, "virtio_gpu_metadata", GPU_VIRTIO_METADATA_REGION_SIZE, physical=True)
    virtio_data = MemoryRegion(sdf, "virtio_gpu_data", GPU_VIRTIO_DATA_REGION_SIZE, physical=True)
    for mr in [virtio_regs, virtio_metadata, virtio_data]:
        sdf.add_mr(mr)
    gpu_driver.add_map(Map(virtio_regs, 0x2_000_000, "rw", cached=False, setvar_vaddr="virtio_regs"))
    gpu_driver.add_map(Map(virtio_metadata, 0x60_000_000, "rw", cached=False, setvar_vaddr="virtio_metadata"))
    gpu_driver.add_map(Map(virtio_data, 0x60_200_000, "rw", cached=False, setvar_vaddr="virtio_data"))
    gpu_driver.add_irq(Irq(mmio.devices["gpu"][1], trigger=Irq.Trigger.EDGE, id=0))

    # Driver <-> virtualiser
    driver_queues = add_gpu_queues(sdf, "gpu_driver")
    driver_data = MemoryRegion(sdf, "gpu_driver_data", GPU_DRIVER_DATA_REGION_SIZE)
    sdf.add_mr(driver_data)
    map_gpu_queues(gpu_driver, driver_queues, 0x40_000_000, ("gpu_events", "gpu_req_queue", "gpu_resp_queue"))
    gpu_driver.add_map(Map(driver_data, 0x40_600_000, "rw", setvar_vaddr="gpu_driver_data"))
    map_gpu_queues(gpu_virt, driver_queues, 0x40_000_000,
                   ("gpu_driver_events", "gpu_driver_req_queue", "gpu_driver_resp_queue"))
    gpu_virt.add_map(Map(driver_data, 0x40_600_000, "rw", setvar_vaddr="gpu_driver_data"))

    # Virtualiser <-> compositor. The data region holds the compositor's framebuffer
    # and the device reads it directly, so it is physical as well.
    client_queues = add_gpu_queues(sdf, "gpu_compositor")
    client_data = MemoryRegion(sdf, "gpu_compositor_data", GPU_CLIENT_DATA_REGION_SIZE, physical=True)
    sdf.add_mr(client_data)
    map_gpu_queues(gpu_virt, client_queues, 0x30_000_000,
                   ("gpu_client_events", "gpu_client_req_queue", "gpu_client_resp_queue"))
    gpu_virt.add_map(Map(client_data, 0x30_600_000, "rw", setvar_vaddr="gpu_client_data"))
    map_gpu_queues(compositor, client_queues, 0x40_000_000, ("gpu_events", "gpu_req_queue", "gpu_resp_queue"))
    compositor.add_map(Map(client_data, 0x40_800_000, "rw", setvar_vaddr="gpu_data"))

    # Channel ids are fixed by the GPU components: the virtualiser talks to the
    # driver on 0 and to client i on 1 + i, the driver talks to it on 1, and
    # the compositor uses 0 (VIRT_CH in src/compositor.c).
    sdf.add_channel(Channel(gpu_virt, gpu_driver, a_id=0, b_id=1))
    sdf.add_channel(Channel(compositor, gpu_virt, a_id=0, b_id=1))

    # Input: one driver PD per device, all feeding the input virtualiser,
    # which delivers to the compositor. Channel ids must match
    # components/input and INPUT_CH in src/compositor.c.
    input_virt = ProtectionDomain("input_virt", "input_virt.elf", priority=200)
    input_drivers = []
    for i, name in enumerate(INPUT_DEVICES):
        regs_offset, irq = mmio.devices[name]
        driver = ProtectionDomain(f"input_{name}", f"input_{name}.elf", priority=253)
        input_drivers.append(driver)

        driver.add_map(Map(virtio_regs, 0x2_000_000, "rw", cached=False, setvar_vaddr="device_regs"))
        dma = MemoryRegion(sdf, f"input_{name}_dma", INPUT_VIRTIO_DMA_SIZE, physical=True)
        sdf.add_mr(dma)
        driver.add_map(Map(dma, 0x3_000_000, "rw", cached=False, setvar_vaddr="virtio_dma"))
        driver.add_irq(Irq(irq, trigger=Irq.Trigger.EDGE, id=0))

        queue = MemoryRegion(sdf, f"input_{name}_queue", INPUT_QUEUE_REGION_SIZE)
        sdf.add_mr(queue)
        driver.add_map(Map(queue, 0x4_000_000, "rw", setvar_vaddr="input_queue"))
        input_virt.add_map(Map(queue, 0x4_000_000 + i * INPUT_QUEUE_REGION_SIZE, "rw",
                               setvar_vaddr="input_driver_queues" if i == 0 else None))
        sdf.add_channel(Channel(input_virt, driver, a_id=i, b_id=1))

        with open(f"{output_dir}/input_driver_{name}.data", "wb") as f:
            f.write(struct.pack("<III", INPUT_DRIVER_CONFIG_MAGIC, regs_offset, INPUT_QUEUE_CAPACITY))

    compositor_input_queue = MemoryRegion(sdf, "input_compositor_queue", INPUT_QUEUE_REGION_SIZE)
    sdf.add_mr(compositor_input_queue)
    input_virt.add_map(Map(compositor_input_queue, 0x5_000_000, "rw", setvar_vaddr="input_client_queues"))
    compositor.add_map(Map(compositor_input_queue, 0x5_000_000, "rw", setvar_vaddr="input_queue"))
    sdf.add_channel(Channel(input_virt, compositor, a_id=len(INPUT_DEVICES), b_id=2))

    # Applications. Each gets a surface and a state page that the compositor
    # maps read-only, and an event queue from the compositor.
    apps = {}
    for i, name in enumerate(GUI_APPS):
        app = ProtectionDomain(name, f"{name}.elf", priority=5, stack_size=0x10000)
        apps[name] = app

        surface = MemoryRegion(sdf, f"gui_{name}_surface", GUI_SURFACE_REGION_SIZE)
        state = MemoryRegion(sdf, f"gui_{name}_state", GUI_STATE_REGION_SIZE)
        events = MemoryRegion(sdf, f"gui_{name}_events", GUI_EVENTS_REGION_SIZE)
        for mr in [surface, state, events]:
            sdf.add_mr(mr)

        app.add_map(Map(surface, 0x20_000_000, "rw", setvar_vaddr="gui_surface"))
        app.add_map(Map(state, 0x21_000_000, "rw", setvar_vaddr="gui_state"))
        app.add_map(Map(events, 0x22_000_000, "rw", setvar_vaddr="gui_events"))

        first = i == 0
        compositor.add_map(Map(surface, 0x60_000_000 + i * GUI_SURFACE_REGION_SIZE, "r",
                               setvar_vaddr="gui_surfaces" if first else None))
        compositor.add_map(Map(state, 0x61_000_000 + i * GUI_STATE_REGION_SIZE, "r",
                               setvar_vaddr="gui_states" if first else None))
        compositor.add_map(Map(events, 0x62_000_000 + i * GUI_EVENTS_REGION_SIZE, "rw",
                               setvar_vaddr="gui_events" if first else None))

        sdf.add_channel(Channel(compositor, app, a_id=GUI_APP_CH_BASE + i, b_id=0))

    timer_system.add_client(apps["clock"])

    for pd in [timer_driver, gpu_driver, gpu_virt, input_virt, *input_drivers, compositor, *apps.values()]:
        sdf.add_pd(pd)

    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)

    xml = add_region_paddr_setvars(sdf.render(), "gpu_driver", [
        ("virtio_metadata_paddr", "virtio_gpu_metadata"),
        ("virtio_data_paddr", "virtio_gpu_data"),
        ("gpu_client_data_paddr", "gpu_compositor_data"),
    ])
    for name in INPUT_DEVICES:
        xml = add_region_paddr_setvars(xml, f"input_{name}", [("virtio_dma_paddr", f"input_{name}_dma")])
    with open(f"{output_dir}/{sdf_path}", "w+") as f:
        f.write(xml)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtb", required=True)
    parser.add_argument("--sddf", required=True)
    parser.add_argument("--board", required=True, choices=[b for b in VIRTIO_MMIO])
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)

    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb)
