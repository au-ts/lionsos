# 0. Run in examples/vulkan_test

# 1. Compile main.c targeting ARM64

clang -c -ffreestanding -mstrict-align -mcpu=cortex-a53 -target aarch64-none-elf -g3 -O2 \
 -I/Users/personal/Documents/University/lionsos/microkit-sdk-2.2.0/board/qemu_virt_aarch64/debug/include \
 src/manager.c -o main.o

# 2. Link your objects with the Microkit platform runtime library

ld.lld -L/Users/personal/Documents/University/lionsos/microkit-sdk-2.2.0/board/qemu_virt_aarch64/debug/lib \
 -o graphics_core.elf -lmicrokit -Tmicrokit.ld main.o

# 3. Use the Python script to build the bootable system image asset

/Users/personal/Documents/University/lionsos/microkit-sdk-2.2.0/bin/microkit \
 system.sdf --search-path . --board qemu_virt_aarch64 --config debug -o minimal_graphics.img

# 4. Run in QEMU

qemu-system-aarch64 -machine virt,virtualization=on \
 -cpu cortex-a53 \
 -serial mon:stdio \
 -m size=2G \
 -device virtio-gpu-device,bus=virtio-mmio-bus.8 \
 -device virtio-gpu-pci \
 -device virtio-keyboard-pci \
 -device loader,file=minimal_graphics.img,addr=0x70000000,cpu-num=0 \
 -global virtio-mmio.force-legacy=false
