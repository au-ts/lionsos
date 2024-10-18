# Makefile for webserver.
#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This makefile will be copied into the Build directory and used from there.
#
BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
SDDF := $(LIONSOS)/dep/sddf

ifeq (${MICROKIT_BOARD},odroidc4)
	TIMER_DRIVER_DIR := meson
	ETHERNET_DRIVER_DIR := meson
	UART_DRIVER_DIR := meson
	CPU := cortex-a55
else ifeq (${MICROKIT_BOARD},qemu_virt_aarch64)
	TIMER_DRIVER_DIR := arm
	ETHERNET_DRIVER_DIR := virtio
	UART_DRIVER_DIR := arm
	CPU := cortex-a53
	QEMU := qemu-system-aarch64
else
$(error Unsupported MICROKIT_BOARD given)
endif

TOOLCHAIN := clang
CC := clang
LD := ld.lld
AR := llvm-ar
CC_USERLEVEL := zig cc
RANLIB := llvm-ranlib
TARGET := aarch64-none-elf
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit

NFS=$(LIONSOS)/components/fs/nfs
MUSL_SRC := $(LIONSOS)/dep/musllibc
MUSL := musllibc
MICRODOT := ${LIONSOS}/dep/microdot/src
LIBVMM_TOOLS := $(LIBVMM)/tools
BLK_COMPONENTS := $(SDDF)/blk/components
SYSTEM_DIR := $(VIRTIO_EXAMPLE)/board/$(MICROKIT_BOARD)

LWIP := $(SDDF)/network/ipstacks/lwip/src
FAT := $(LIONSOS)/components/fs/fat

BLK_DRIVER_VM_USERLEVEL := uio_blk_driver
BLK_DRIVER_VM_USERLEVEL_INIT := blk_driver_init

vpath %.c $(SDDF) $(LIBVMM) $(VIRTIO_EXAMPLE)

SYSTEM_FILE := $(WEBSERVER_SRC_DIR)/board/$(MICROKIT_BOARD)/webserver.system

CFLAGS := \
	-mtune=$(CPU) \
	-mstrict-align \
	-ffreestanding \
	-O2 \
	-MD \
	-MP \
	-Wall \
	-Wno-unused-function \
	-I$(BOARD_DIR)/include \
	-target $(TARGET) \
	-DBOARD_$(MICROKIT_BOARD) \
	-I$(LIONSOS)/include \
	-I$(LIBVMM)/include \
	-I$(SDDF)/include \
	-I$(EXAMPLE_DIR)/include \
	-I$(CONFIG_INCLUDE)

CFLAGS_USERLEVEL := \
		-g3 \
		-O3 \
		-Wno-unused-command-line-argument \
		-Wall -Wno-unused-function \
		-D_GNU_SOURCE \
		-target aarch64-linux-gnu \
		-I$(EXAMPLE_DIR) \
		-I$(BOARD_DIR)/include \
		-I$(CONFIG_INCLUDE) \
		-I$(SDDF)/include

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a libvmm.a

IMAGE_FILE := webserver.img
REPORT_FILE := report.txt

SDDF_MAKEFILES := ${SDDF}/util/util.mk \
		  ${SDDF}/drivers/timer/${TIMER_DRIVER_DIR}/timer_driver.mk \
		  ${SDDF}/drivers/network/${ETHERNET_DRIVER_DIR}/eth_driver.mk \
		  ${SDDF}/drivers/serial/${UART_DRIVER_DIR}/uart_driver.mk \
		  ${SDDF}/network/components/network_components.mk \
		  ${SDDF}/serial/components/serial_components.mk \
		  ${SDDF}/libco/libco.mk


include ${SDDF_MAKEFILES}
include $(NFS)/nfs.mk
include $(BLK_COMPONENTS)/blk_components.mk
include $(LIBVMM)/vmm.mk
include $(LIBVMM_TOOLS)/linux/uio/uio.mk
include $(LIBVMM_TOOLS)/linux/blk/blk_init.mk
include $(LIBVMM_TOOLS)/linux/uio_drivers/blk/uio_blk.mk

IMAGES := timer_driver.elf eth_driver.elf micropython.elf nfs.elf \
	  copy.elf network_virt_rx.elf network_virt_tx.elf \
	  uart_driver.elf serial_virt_tx.elf $(BLK_IMAGES) \
	  blk_driver_vmm.elf

all: $(IMAGE_FILE)

-include vmm.d

${IMAGES}: libsddf_util_debug.a libvmm.a

CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@

micropython.elf: mpy-cross manifest.py webserver.py config.py \
		${MICRODOT} ${LIONSOS}/dep/libmicrokitco/Makefile
	make -C $(LIONSOS)/components/micropython -j$(nproc) \
			MICROKIT_SDK=$(MICROKIT_SDK) \
			MICROKIT_BOARD=$(MICROKIT_BOARD) \
			MICROKIT_CONFIG=$(MICROKIT_CONFIG) \
			MICROPY_MPYCROSS=$(abspath mpy_cross/mpy-cross) \
			MICROPY_MPYCROSS_DEPENDENCY=$(abspath mpy_cross/mpy-cross) \
			BUILD=$(abspath .) \
			LIBMATH=$(LIBMATH) \
			LIBMATH=$(abspath $(BUILD_DIR)/libm) \
			CONFIG_INCLUDE=$(abspath $(CONFIG_INCLUDE)) \
			FROZEN_MANIFEST=$(abspath ./manifest.py) \
			EXEC_MODULE=webserver.py

config.py: ${CHECK_FLAGS_BOARD_MD5}
	echo "base_dir='$(WEBSITE_DIR)'" > config.py

%.py: ${WEBSERVER_SRC_DIR}/%.py
	cp $< $@

fat.elf: musllibc/lib/libc.a
	make -C $(LIONSOS)/components/fs/fat \
		CC=$(CC) \
		LD=$(LD) \
		CPU=$(CPU) \
		BUILD_DIR=$(abspath .) \
		CONFIG=$(MICROKIT_CONFIG) \
		MICROKIT_SDK=$(MICROKIT_SDK) \
		MICROKIT_BOARD=$(MICROKIT_BOARD) \
		LIBC_DIR=$(abspath $(BUILD_DIR)/musllibc) \
		BUILD_DIR=$(abspath .) \
		CONFIG_INCLUDE=$(abspath $(CONFIG_INCLUDE)) \
		TARGET=$(TARGET)

$(MUSL)/lib/libc.a $(MUSL)/include: ${MUSL}/Makefile
	make -C $(MUSL_SRC) \
		C_COMPILER=aarch64-none-elf-gcc \
		TOOLPREFIX=aarch64-none-elf- \
		CONFIG_ARCH_AARCH64=y \
		STAGE_DIR=$(abspath $(MUSL)) \
		SOURCE_DIR=.

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	RUST_BACKTRACE=full $(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

qemu: ${IMAGE_FILE}
	$(QEMU) -machine virt,virtualization=on \
			-cpu cortex-a53 \
			-serial mon:stdio \
			-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
			-m size=2G \
			-nographic \
			-device virtio-net-device,netdev=netdev0 \
			-netdev user,id=netdev0,hostfwd=tcp::5555-10.0.2.16:80 \
			-global virtio-mmio.force-legacy=false

FORCE: ;

mpy-cross: FORCE  ${LIONSOS}/dep/micropython/mpy-cross
	make -C $(LIONSOS)/dep/micropython/mpy-cross BUILD=$(abspath ./mpy_cross)

.PHONY: mpy-cross

$(LIONSOS)/dep/micropython/py/mkenv.mk ${LIONSOS}/dep/micropython/mpy-cross:
	cd ${LIONSOS}; git submodule update --init dep/micropython
	cd ${LIONSOS}/dep/micropython && git submodule update --init lib/micropython-lib
${LIONSOS}/dep/libmicrokitco/Makefile:
	cd ${LIONSOS}; git submodule update --init dep/libmicrokitco

${MICRODOT}:
	cd ${LIONSOS}; git submodule update --init dep/microdot

${MUSL}/Makefile:
	cd ${LIONSOS}; git submodule update --init dep/musllibc

${SDDF_MAKEFILES} &:
	cd ${LIONSOS}; git submodule update --init dep/sddf

# Stolen from virtio example
%_vmm.elf: %_vm/vmm.o %_vm/images.o
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

%_vm:
	mkdir -p $@

blk_driver_vm/rootfs.cpio.gz: $(SYSTEM_DIR)/blk_driver_vm/rootfs.cpio.gz \
	$(BLK_DRIVER_VM_USERLEVEL) $(BLK_DRIVER_VM_USERLEVEL_INIT) |blk_driver_vm
	$(LIBVMM)/tools/packrootfs $(SYSTEM_DIR)/blk_driver_vm/rootfs.cpio.gz \
		blk_driver_vm/rootfs -o $@ \
		--startup $(BLK_DRIVER_VM_USERLEVEL_INIT) \
		--home $(BLK_DRIVER_VM_USERLEVEL)

%_vm/vm.dts: $(SYSTEM_DIR)/%_vm/dts/linux.dts \
	$(SYSTEM_DIR)/%_vm/dts/overlays/*.dts $(CHECK_FLAGS_BOARD_MD5) |%_vm
	$(LIBVMM)/tools/dtscat $^ > $@

%_vm/vm.dtb: %_vm/vm.dts |%_vm
	$(DTC) -q -I dts -O dtb $< > $@

%_vm/vmm.o: $(VIRTIO_EXAMPLE)/%_vmm.c $(CHECK_FLAGS_BOARD_MD5) |%_vm
	$(CC) $(CFLAGS) -c -o $@ $<

%_vm/images.o: $(LIBVMM)/tools/package_guest_images.S $(CHECK_FLAGS_BOARD_MD5) \
	$(SYSTEM_DIR)/%_vm/linux %_vm/vm.dtb %_vm/rootfs.cpio.gz
	$(CC) -c -g3 -x assembler-with-cpp \
					-DGUEST_KERNEL_IMAGE_PATH=\"$(SYSTEM_DIR)/$(@D)/linux\" \
					-DGUEST_DTB_IMAGE_PATH=\"$(@D)/vm.dtb\" \
					-DGUEST_INITRD_IMAGE_PATH=\"$(@D)/rootfs.cpio.gz\" \
					-target $(TARGET) \
					$(LIBVMM)/tools/package_guest_images.S -o $@
