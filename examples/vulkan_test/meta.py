# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
from importlib.metadata import version
from sdfgen import SystemDescription, Sddf, Vmm, DeviceTree
from board import BOARDS

assert version("sdfgen").split(".")[1] == "28", "Unexpected sdfgen version"

ProtectionDomain = SystemDescription.ProtectionDomain
VirtualMachine = SystemDescription.VirtualMachine
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Irq = SystemDescription.IrqConventional
Channel = SystemDescription.Channel


def generate(sdf_path: str, output_dir: str, dtb: DeviceTree, guest_dtb: DeviceTree):
    # --- 1. Base Drivers ---
    serial_node = dtb.node(board.serial)
    assert serial_node is not None
    timer_node = dtb.node(board.timer)
    assert timer_node is not None

    timer_driver = ProtectionDomain("timer_driver", "timer_driver.elf", priority=101)
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    serial_driver = ProtectionDomain("serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_virt_rx = ProtectionDomain("serial_virt_rx", "serial_virt_rx.elf", priority=99)
    serial_system = Sddf.Serial(sdf, serial_node, serial_driver, serial_virt_tx, virt_rx=serial_virt_rx)

    # --- 2. Virtual Machine Monitor (VMM) ---
    vmm = ProtectionDomain("framebuffer_vmm", "vmm.elf", priority=1)
    display_manager = ProtectionDomain("display_manager", "display_manager.elf", priority=2)
    vm = VirtualMachine("linux", [VirtualMachine.Vcpu(id=0)])
    vmm_system = Vmm(sdf, vmm, vm, guest_dtb, one_to_one_ram=True)

    # Framebuffer memory region
    framebuffer = MemoryRegion(sdf, "framebuffer", 0x2_000_000)
    sdf.add_mr(framebuffer)
    framebuffer_map = Map(framebuffer, 0x30000000, "rw")
    vm.add_map(framebuffer_map)
    display_manager.add_map(framebuffer_map)

    sdf.add_channel(Channel(vmm, display_manager, a_id=1, b_id=1))

    # --- 3. Hardware Passthrough ---
    if board.name == "qemu_virt_aarch64":
        passthrough_irqs = []
        devices = []
        # Pass through the GPU's virtio-mmio page (bus.8 = 0xa001000).
        # vmm_system.add_passthrough_device(dtb.node("virtio_mmio@a001000"))

        gpu_mmio = MemoryRegion(sdf, "gpu_mmio", 0x1000, paddr=0xA001000)
        sdf.add_mr(gpu_mmio)
        display_manager.add_map(Map(gpu_mmio, 0xA001000, "rw", cached=False))

    elif board.name == "odroidc4":
        passthrough_irqs = [Irq(5)]
        devices = []
        # Odroid hardware passthrough (Left intact for future portability)
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ff600000/hdmi-tx@0"))
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ff600000/bus@30000"))
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ff600000/audio-controller@32000"))
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ff600000/bus@38000/video-lut@48"))
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ff600000/phy@3a000"))
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ff600000/phy@46000"))
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ff600000/mdio-multiplexer@4c000"))
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ff600000/bus@60000"))
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ff600000/audio-controller@61000"))
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ff800000/sys-ctrl@0"))
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ff800000/cec@100"), irqs=[])
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ff800000/ao-secure@140"))
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ff800000/cec@280"), irqs=[])
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ff800000/pwm@2000"))
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ff800000/ir@8000"))
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ff800000/adc@9000"))
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ffd00000/reset-controller@1004"))
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ffd00000/spi@13000"), irqs=[])
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ffd00000/spi@14000"))
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ffd00000/spi@15000"), irqs=[])
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ffd00000/pwm@19000"))
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ffd00000/pwm@1a000"))
        vmm_system.add_passthrough_device(dtb.node("soc/bus@ffd00000/pwm@1b000"))
        vmm_system.add_passthrough_device(dtb.node("soc/usb@ffe09000"))
        vmm_system.add_passthrough_device(dtb.node("soc/usb@ffe09000/usb@ff400000"))
        vmm_system.add_passthrough_device(dtb.node("soc/usb@ffe09000/usb@ff500000"))
        vmm_system.add_passthrough_device(dtb.node("soc/gpu@ffe40000"))
        vmm_system.add_passthrough_device(dtb.node("soc/vpu@ff900000"), regions=[0])

    for irq in passthrough_irqs:
        vmm_system.add_passthrough_irq(irq)

    for d in devices:
        mr = MemoryRegion(sdf, d[0], d[1], paddr=d[2])
        sdf.add_mr(mr)
        vm.add_map(Map(mr, d[2], "rw", cached=False))

    serial_system.add_client(vmm)
    timer_system.add_client(vmm)

    # --- 4. Register PDs ---
    pds = [
        serial_driver,
        serial_virt_tx,
        serial_virt_rx,
        timer_driver,
        vmm,
        display_manager,
    ]
    for pd in pds:
        sdf.add_pd(pd)

    # --- 5. Connect and Serialize ---
    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)
    assert vmm_system.connect()
    assert vmm_system.serialise_config(output_dir)

    with open(f"{output_dir}/{sdf_path}", "w+") as f:
        f.write(sdf.render())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtb", required=True)
    parser.add_argument("--sddf", required=True)
    parser.add_argument("--board", required=True, choices=[b.name for b in BOARDS])
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)
    parser.add_argument("--guest-dtb", required=True)
    # Note: Removed --nfs-server and --nfs-dir because our Makefile no longer passes them.

    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    with open(args.guest_dtb, "rb") as f:
        guest_dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb, guest_dtb)
