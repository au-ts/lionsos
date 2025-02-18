#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
IMAGES := \
	timer_driver.elf \
	eth_driver.elf \
	micropython.elf \
	fs_driver_vmm.elf \
	copy.elf \
	network_virt_rx.elf \
	network_virt_tx.elf \
	uart_driver.elf \
	serial_virt_rx.elf \
	serial_virt_tx.elf \
	blk_virt.elf

ifeq ($(strip $(MICROKIT_BOARD)), maaxboard)
	NET_DRIV_DIR := imx
	BLK_DRIV_DIR := mmc/imx
	UART_DRIV_DIR := imx
	TIMER_DRIV_DIR := imx
	IMAGES += mmc_driver.elf
	BLK_MK := mmc_driver.mk
	CPU := cortex-a53
else ifeq ($(strip $(MICROKIT_BOARD)), qemu_virt_aarch64)
	NET_DRIV_DIR := virtio
	BLK_DRIV_DIR := virtio
	UART_DRIV_DIR := arm
	TIMER_DRIV_DIR := arm
	IMAGES += blk_driver.elf
	BLK_MK := blk_driver.mk
	CPU := cortex-a53
	QEMU := qemu-system-aarch64
else
	$(error Unsupported MICROKIT_BOARD given)
endif

TOOLCHAIN := clang
CC := clang
CC_USERLEVEL := zig cc
LD := ld.lld
RANLIB := llvm-ranlib
AR := llvm-ar
AS := llvm-as
TARGET := aarch64-none-elf
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
DTC := dtc

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
PLATFORM := meson
SDDF := $(LIONSOS)/dep/sddf
LIBVMM_DIR := $(LIONSOS)/dep/libvmm
LIBVMM_TOOLS := $(LIBVMM_DIR)/tools

LWIP := $(SDDF)/network/ipstacks/lwip/src

CFLAGS := \
	-mtune=$(CPU) \
	-mstrict-align \
	-ffreestanding \
	-g3 \
	-O3 \
	-Wall \
	-Wno-unused-function \
	-I$(BOARD_DIR)/include \
	-target $(TARGET) \
	-DBOARD_$(MICROKIT_BOARD) \
	-I$(SDDF)/include \
	-I${CONFIG_INCLUDE} \
	-I$(LIBVMM_DIR)/include \
	-I$(LIBVMM_DIR)/tools/linux/include \
	-DVIRTIO_MMIO_NET_OFFSET=0xc00 

CFLAGS_USERLEVEL := \
	-g3 \
	-O3 \
	-Wno-unused-command-line-argument \
	-Wall -Wno-unused-function \
	-D_GNU_SOURCE \
	-target aarch64-linux-gnu \
	-I$(BOARD_DIR)/include \
	-I$(SDDF)/include \
	-I$(LIONSOS)/include

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a

IMAGE_FILE := vfs.img
REPORT_FILE := report.txt

all: cache.o
CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@


%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

BLK_DRIVER := $(SDDF)/drivers/blk/${BLK_DRIV_DIR}
BLK_COMPONENTS := $(SDDF)/blk/components

include ${SDDF}/util/util.mk
include ${LIBVMM_DIR}/vmm.mk
include ${LIBVMM_DIR}/tools/linux/uio_drivers/fs/uio_fs.mk
include ${LIBVMM_DIR}/tools/linux/fs/fs_init.mk
include ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk
include ${SDDF}/drivers/network/${NET_DRIV_DIR}/eth_driver.mk
include ${SDDF}/drivers/serial/${UART_DRIV_DIR}/uart_driver.mk
include ${SDDF}/network/components/network_components.mk
include ${SDDF}/serial/components/serial_components.mk
include ${SDDF}/libco/libco.mk
include ${BLK_DRIVER}/${BLK_MK}
include ${BLK_COMPONENTS}/blk_components.mk

micropython.elf: mpy-cross libsddf_util_debug.a libco.a
	cp $(EXAMPLE_DIR)/fs_test.py .
	cp $(EXAMPLE_DIR)/manifest.py .
	cp $(EXAMPLE_DIR)/bench.py .
	make  -C $(LIONSOS)/components/micropython -j$(nproc) \
		MICROKIT_SDK=$(MICROKIT_SDK) \
		MICROKIT_BOARD=$(MICROKIT_BOARD) \
		MICROKIT_CONFIG=$(MICROKIT_CONFIG) \
		MICROPY_MPYCROSS=$(abspath mpy_cross/mpy-cross) \
		MICROPY_MPYCROSS_DEPENDENCY=$(abspath mpy_cross/mpy-cross) \
		BUILD=$(abspath .) \
		LIBMATH=${LIBMATH} \
		CONFIG_INCLUDE=$(abspath $(CONFIG_INCLUDE)) \
		FROZEN_MANIFEST=$(abspath ./manifest.py) \
		V=1

# Compile the FS Driver VM
vpath %.c $(LIBVMM_DIR)
SYSTEM_DIR := $(EXAMPLE_DIR)/board/$(MICROKIT_BOARD)
FS_VM_USERLEVEL := uio_fs_driver
FS_VM_USERLEVEL_INIT := fs_driver_init

rootfs.cpio.gz: $(SYSTEM_DIR)/fs_driver_vm/rootfs.cpio.gz \
	$(FS_VM_USERLEVEL) $(FS_VM_USERLEVEL_INIT)
	$(LIBVMM_TOOLS)/packrootfs $(SYSTEM_DIR)/fs_driver_vm/rootfs.cpio.gz \
		rootfs -o $@ \
		--startup $(FS_VM_USERLEVEL_INIT) \
		--home $(FS_VM_USERLEVEL)

fs_vm.dts: $(SYSTEM_DIR)/fs_driver_vm/dts/linux.dts $(SYSTEM_DIR)/fs_driver_vm/dts/overlays/*.dts
	$(LIBVMM_TOOLS)/dtscat $^ > $@

fs_vm.dtb: fs_vm.dts
	$(DTC) -q -I dts -O dtb $< > $@

fs_driver_vm_image.o: $(LIBVMM_TOOLS)/package_guest_images.S $(CHECK_FLAGS_BOARD_MD5) \
	$(SYSTEM_DIR)/fs_driver_vm/linux fs_vm.dtb rootfs.cpio.gz
	$(CC) -c -g3 -x assembler-with-cpp \
					-DGUEST_KERNEL_IMAGE_PATH=\"$(SYSTEM_DIR)/fs_driver_vm/linux\" \
					-DGUEST_DTB_IMAGE_PATH=\"fs_vm.dtb\" \
					-DGUEST_INITRD_IMAGE_PATH=\"rootfs.cpio.gz\" \
					-target $(TARGET) \
					$(LIBVMM_TOOLS)/package_guest_images.S -o $@

fs_driver_vmm.o: $(EXAMPLE_DIR)/src/fs_vmm.c
	${CC} ${CFLAGS} -c -o $@ $<

fs_driver_vmm.elf: fs_driver_vmm.o fs_driver_vm_image.o libvmm.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

#

${IMAGES}: libsddf_util_debug.a

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) ${EXAMPLE_DIR}/board/$(MICROKIT_BOARD)/vfs.system
	$(MICROKIT_TOOL) ${EXAMPLE_DIR}/board/$(MICROKIT_BOARD)/vfs.system \
		--search-path $(BUILD_DIR) \
		--board $(MICROKIT_BOARD) \
		--config $(MICROKIT_CONFIG) \
		-o $(IMAGE_FILE) \
		-r $(REPORT_FILE)

FORCE:

%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

mpy-cross: FORCE
	${MAKE} -C ${LIONSOS}/dep/micropython/mpy-cross BUILD=$(abspath ./mpy_cross)

qemu_disk.img:
	$(LIONSOS)/dep/sddf/examples/blk/mkvirtdisk $@ 1 512 16777216

qemu: ${IMAGE_FILE}
	$(QEMU) -machine virt,virtualization=on \
		-cpu cortex-a53 \
		-serial mon:stdio \
		-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
		-m size=2G \
		-nographic \
		-global virtio-mmio.force-legacy=false \
		-d guest_errors \
		-drive file=/Users/dreamliner787-9/ext4_disk_64mb.img,if=none,format=raw,id=hd \
		-device virtio-blk-device,drive=hd \
		-device virtio-net-device,netdev=netdev0 \
		-netdev user,id=netdev0
