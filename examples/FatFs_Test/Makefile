#
# Copyright 2021, Breakaway Consulting Pty. Ltd.
# Copyright 2022, UNSW (ABN 57 195 873 179)
#
# SPDX-License-Identifier: BSD-2-Clause
#

# Command to compile and run the img
#  make BOARD=qemu_arm_virt MICROKIT_SDK=~/Sel4/libvmm-blk_benchmark/examples/blk_benchmark/sdk qemu



ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif

ifeq ($(strip $(BOARD)),)
$(error BOARD must be specified)
endif

# Default build directory, pass BUILD_DIR=<dir> to override
BUILD_DIR ?= build
# Default config is a debug build, pass CONFIG=<debug/release/benchmark> to override
CONFIG ?= debug

# @ivanv: Check for dependencies and make sure they are installed/in the path

# @ivanv: check that all dependencies exist
# Specify that we use bash for all shell commands
SHELL=/bin/bash
# All dependencies needed to compile the VMM
QEMU := qemu-system-aarch64
DTC := dtc

ifndef TOOLCHAIN
	# Get whether the common toolchain triples exist
	TOOLCHAIN_AARCH64_LINUX_GNU := $(shell command -v aarch64-linux-gnu-gcc 2> /dev/null)
	TOOLCHAIN_AARCH64_UNKNOWN_LINUX_GNU := $(shell command -v aarch64-unknown-linux-gnu-gcc 2> /dev/null)
	# Then check if they are defined and select the appropriate one
	ifdef TOOLCHAIN_AARCH64_LINUX_GNU
		TOOLCHAIN := aarch64-linux-gnu
	else ifdef TOOLCHAIN_AARCH64_UNKNOWN_LINUX_GNU
		TOOLCHAIN := aarch64-unknown-linux-gnu
	else
		$(error "Could not find an AArch64 cross-compiler")
	endif
endif

CC := clang
LD := ld.lld
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit

# @ivanv: need to have a step for putting in the initrd node into the DTB,
# 		  right now it is unfortunately hard-coded.

# @ivanv: check that the path of SDK_PATH/BOARD exists
# @ivanv: Have a list of supported boards to check with, if it's not one of those
# have a helpful message that lists all the support boards.

# @ivanv: incremental builds don't work with IMAGE_DIR changing
BOARD_DIR := $(MICROKIT_SDK)/board/$(BOARD)/$(CONFIG)
VMM := ../../vmm
VMM_TOOLS := $(VMM)/tools
VMM_SRC_DIR := $(VMM)/src
VMM_EXAMPLE := $(VMM)/examples
SYSTEM_DESCRIPTION := FatFs.system

CURRENT_DIR := $(shell pwd)

LINUX_DIR := $(VMM_TOOLS)/linux
UIO_DRIVERS_DIR := $(LINUX_DIR)/uio_drivers

BLK_DRIVER_VM_DIR := $(VMM)/examples/virtio/board/$(BOARD)/blk_driver_vm
BLK_DRIVER_VM_DTS_DIR := $(BLK_DRIVER_VM_DIR)/dts
BLK_DRIVER_VM_ROOTFS_TMP_DIR := $(BUILD_DIR)/blk_driver_vm_rootfs

BLK_DRIVER_VM_LINUX := $(BLK_DRIVER_VM_DIR)/linux
BLK_DRIVER_VM_INITRD := $(BLK_DRIVER_VM_DIR)/rootfs.cpio.gz
BLK_DRIVER_VM_INITRD_OVERLAYED := $(BLK_DRIVER_VM_DIR)/rootfs_overlayed.cpio.gz
BLK_DRIVER_VM_BASE_DTS := $(BLK_DRIVER_VM_DTS_DIR)/linux.dts
BLK_DRIVER_VM_DTS_OVERLAYS :=	$(BLK_DRIVER_VM_DTS_DIR)/init.dts \
								$(BLK_DRIVER_VM_DTS_DIR)/uio.dts

BLK_DRIVER_VM_DTB := $(BUILD_DIR)/blk_driver_vm.dtb

SDDF := ../../vmm/examples/virtio/sddf

SDDF_SERIAL_RINGBUFFER := $(SDDF)/serial/libserialsharedringbuffer
SDDF_SERIAL_COMPONENTS := $(SDDF)/serial/components
SDDF_SERIAL_DRIVER := $(SDDF)/drivers/serial/arm

SDDF_BLK_SHAREDQUEUE := $(SDDF)/blk/libblksharedqueue

ELFS := FatFs.elf blk_driver_vmm.elf Client.elf

BLK_DRIVER_VM_USERLEVEL := uio_blk_driver storage_0 storage_1
BLK_DRIVER_VM_USERLEVEL_INIT := blk_driver_init

IMAGE_FILE = $(BUILD_DIR)/loader.img
REPORT_FILE = $(BUILD_DIR)/report.txt

MUSL=../../musllibc

# @ivanv: should only compile printf.o in debug
VMM_OBJS := printf.o \
			bitarray.o \
			virq.o \
			linux.o \
			guest.o \
			psci.o \
			smc.o \
			fault.o \
			util.o \
			vgic.o \
			vgic_v2.o \
			tcb.o \
			vcpu.o \

BLK_DRIVER_VMM_OBJS :=	$(VMM_OBJS) \
					blk_driver_images.o \
					blk_driver_vmm.o \
					virtio.o \
					console.o \
					block.o \
					mmio.o \
					sddf_serial_sharedringbuffer.o \
					sddf_blk_shared_queue.o \

SERIAL_MUX_TX_OBJS := mux_tx.o sddf_serial_sharedringbuffer.o
SERIAL_MUX_RX_OBJS := mux_rx.o sddf_serial_sharedringbuffer.o
SERIAL_DRIVER_OBJS := uart.o sddf_serial_sharedringbuffer.o

UIO_BLK_DRIVER_OBJS := blk.o libuio.o sddf_blk_shared_queue.o

# Toolchain flags
# FIXME: For optimisation we should consider providing the flag -mcpu.
# FIXME: We should also consider whether -mgeneral-regs-only should be
# used to avoid the use of the FPU and therefore seL4 does not have to
# context switch the FPU.
# Note we only need -Wno-unused-command-line-argument because in Nix
# passes an extra `--gcc-toolchain` flag which we do not need.
CFLAGS := -mstrict-align \
		  -g3 \
		  -O0 \
		  -ffreestanding \
		  -nostdlib \
		  -Wno-unused-command-line-argument \
		  -Wall -Wno-unused-function \
		  -I$(VMM_SRC_DIR)/arch/aarch64 -I$(VMM_SRC_DIR) -I$(VMM_SRC_DIR)/util -I$(BOARD_DIR)/include \
		  -I$(SDDF_SERIAL_DRIVER)/include \
		  -I$(SDDF)/include \
		  -I$(BUILD_DIR)/musllibc/include \
		  -DBOARD_$(BOARD) \
		  -DCONFIG_$(CONFIG) \
		  -target aarch64-none-elf

CFLAGS_LINUX :=	-g3 \
				-O3 \
				-Wno-unused-command-line-argument \
				-Wall -Wno-unused-function -Werror \
				-I$(SDDF)/include \
				-I$(LINUX_DIR)/include

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -L$(BUILD_DIR)/musllibc/lib -lc -lmicrokit -Tmicrokit.ld

all: directories $(IMAGE_FILE)

qemu: all
	# @ivanv: check that the amount of RAM given to QEMU is at least the number of RAM that QEMU is setup with for seL4.
	if ! command -v $(QEMU) &> /dev/null; then echo "Could not find dependency: qemu-system-aarch64"; exit 1; fi
	$(QEMU) -machine virt,virtualization=on,secure=off \
			-cpu cortex-a53 \
			-serial mon:stdio \
			-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
			-m size=2G \
			-nographic

directories:
	$(shell mkdir -p $(BUILD_DIR))

# Unpack rootfs, put the userlevel in /root and userlevel_init in /etc/init.d/S99, repack rootfs
$(BLK_DRIVER_VM_INITRD_OVERLAYED): $(addprefix $(BUILD_DIR)/, $(BLK_DRIVER_VM_USERLEVEL)) $(addprefix $(BUILD_DIR)/, $(BLK_DRIVER_VM_USERLEVEL_INIT))
	rm -r $(BLK_DRIVER_VM_ROOTFS_TMP_DIR) 2> /dev/null; \
	mkdir -p $(BLK_DRIVER_VM_ROOTFS_TMP_DIR) && \
	gunzip -dc $(BLK_DRIVER_VM_INITRD) | fakeroot cpio -idmv -D$(BLK_DRIVER_VM_ROOTFS_TMP_DIR) 2> /dev/null && \
	for f in $(BLK_DRIVER_VM_USERLEVEL); do \
		cp -- "$(BUILD_DIR)/$$f" "$(BLK_DRIVER_VM_ROOTFS_TMP_DIR)/root/$$f"; \
		chmod +x "$(BLK_DRIVER_VM_ROOTFS_TMP_DIR)/root/$$f"; \
	done && \
	for f in $(BLK_DRIVER_VM_USERLEVEL_INIT); do \
		cp -- "$(BUILD_DIR)/$$f" "$(BLK_DRIVER_VM_ROOTFS_TMP_DIR)/etc/init.d/S99$$f"; \
		chmod +x "$(BLK_DRIVER_VM_ROOTFS_TMP_DIR)/etc/init.d/S99$$f"; \
	done && \
	(cd $(BLK_DRIVER_VM_ROOTFS_TMP_DIR) && find . | cpio -o -H newc -R root:root 2> /dev/null | gzip > $(CURRENT_DIR)/$(BLK_DRIVER_VM_INITRD_OVERLAYED))


$(BLK_DRIVER_VM_DTB): $(BLK_DRIVER_VM_BASE_DTS) $(BLK_DRIVER_VM_DTS_OVERLAYS)
	sh $(VMM_TOOLS)/dtscat $^ > $(BUILD_DIR)/blk_driver_vm.dts
	$(DTC) -q -I dts -O dtb $(BUILD_DIR)/blk_driver_vm.dts > $@

$(BUILD_DIR)/blk_driver_images.o: $(VMM_TOOLS)/package_guest_images.S $(BLK_DRIVER_VM_LINUX) $(BLK_DRIVER_VM_DTB) $(BLK_DRIVER_VM_INITRD_OVERLAYED)
	$(CC) -c -g3 -x assembler-with-cpp \
					-DGUEST_KERNEL_IMAGE_PATH=\"$(BLK_DRIVER_VM_LINUX)\" \
					-DGUEST_DTB_IMAGE_PATH=\"$(BLK_DRIVER_VM_DTB)\" \
					-DGUEST_INITRD_IMAGE_PATH=\"$(BLK_DRIVER_VM_INITRD_OVERLAYED)\" \
					-target aarch64-none-elf \
					$< -o $@

$(BUILD_DIR)/%.o: %.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(VMM_SRC_DIR)/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(VMM_SRC_DIR)/util/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(VMM_SRC_DIR)/arch/aarch64/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(VMM_SRC_DIR)/arch/aarch64/vgic/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(VMM_SRC_DIR)/virtio/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

# Compiling block_driver_vmm.o
$(BUILD_DIR)/blk_driver_vmm.o: $(VMM_EXAMPLE)/virtio/blk_driver_vmm.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@
# Finish compiling block_driver_vmm.o

$(BUILD_DIR)/%.o: $(SDDF_SERIAL_COMPONENTS)/%.c Makefile
	$(CC) -c $(CFLAGS) -DSERIAL_NUM_CLIENTS=2 -DSERIAL_TRANSFER_WITH_COLOUR=1 $< -o $@

$(BUILD_DIR)/sddf_serial_sharedringbuffer.o: Makefile
	$(MAKE) -C $(SDDF_SERIAL_RINGBUFFER) MICROKIT_INCLUDE=$(MICROKIT_SDK)
	cp $(SDDF_SERIAL_RINGBUFFER)/sddf_serial_sharedringbuffer.o $(BUILD_DIR)/sddf_serial_sharedringbuffer.o

$(BUILD_DIR)/%.o: $(SDDF_SERIAL_DRIVER)/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/sddf_blk_shared_queue.o: Makefile
	$(MAKE) -C $(SDDF_BLK_SHAREDQUEUE) MICROKIT_INCLUDE=$(MICROKIT_SDK)
	cp $(SDDF_BLK_SHAREDQUEUE)/sddf_blk_shared_queue.o $(BUILD_DIR)/sddf_blk_shared_queue.o

$(BUILD_DIR)/blk_driver_vmm.elf: $(addprefix $(BUILD_DIR)/, $(BLK_DRIVER_VMM_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/serial_mux_tx.elf: $(addprefix $(BUILD_DIR)/, $(SERIAL_MUX_TX_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/serial_mux_rx.elf: $(addprefix $(BUILD_DIR)/, $(SERIAL_MUX_RX_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/uart_driver.elf: $(addprefix $(BUILD_DIR)/, $(SERIAL_DRIVER_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/%.o: $(UIO_DRIVERS_DIR)/%.c Makefile
	$(TOOLCHAIN)-gcc -c $(CFLAGS_LINUX) $< -o $@

$(BUILD_DIR)/postmark: $(LINUX_DIR)/postmark.c Makefile
	$(TOOLCHAIN)-gcc $< -o $@

# Create virtual disk
$(BUILD_DIR)/storage_0: Makefile
	mkfs.vfat -F 32 -S 512 -s 1 -C $@ 5000

$(BUILD_DIR)/storage_1: Makefile
	dd if=/dev/zero of=$@ bs=1K count=100

$(BUILD_DIR)/blk_client_init: $(LINUX_DIR)/blk_client_init Makefile
	cp $< $@

$(BUILD_DIR)/blk_driver_init: $(LINUX_DIR)/blk_driver_init Makefile
	cp $< $@

$(BUILD_DIR)/uio_blk_driver: $(addprefix $(BUILD_DIR)/, $(UIO_BLK_DRIVER_OBJS))
	$(TOOLCHAIN)-gcc $(CFLAGS_LINUX) $^ -o $@

# File system building process
FF_DIR := ../../fs/fat/

FatFs_OBJS :=   ff.o \
				ffsystem.o \
				ffunicode.o \
                AsyncFATFs.o \
				AsyncFATFunc.o \
				Asyncdiskio.o \
				fs_shared_queue.o \
				sddf_blk_shared_queue.o \
				FiberPool.o \
				FiberFlow.o \
				printf.o \
				util.o \

CFLAGS_T := \
	-mstrict-align \
	-ffreestanding \
	-g \
	-O0 \
	-Wall \
	-Wno-unused-function \
	-target aarch64-none-elf \
	-no-integrated-as \

$(BUILD_DIR)/musllibc/lib/libc.a:
	make -C $(MUSL) \
		C_COMPILER=$(CC) \
		TOOLPREFIX="" \
		CONFIG_ARCH_AARCH64=y \
		CONFIG_USER_DEBUG_BUILD=y \
		STAGE_DIR=$(abspath $(BUILD_DIR)/musllibc) \
		CFLAGS_MUSL="-mstrict-align -ffreestanding -g -O0 -Wall -Wno-unused-function -target aarch64-none-elf -no-integrated-as" \
		SOURCE_DIR=.

$(BUILD_DIR)/%.o: $(FF_DIR)/ff15/source/%.c $(BUILD_DIR)/musllibc/lib/libc.a Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(FF_DIR)/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(FF_DIR)/FiberPool/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(FF_DIR)/FiberPool/FiberFlow/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/fs_shared_queue.o: $(FF_DIR)/libfssharedqueue/fs_shared_queue.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/FatFs.elf: $(addprefix $(BUILD_DIR)/, $(FatFs_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@
# File system building end

# Client building process
Client_OBJS :=  FS_testclient.o \
				fs_shared_queue.o \
				FiberPool.o \
				FiberFlow.o \
				printf.o \
				util.o \

$(BUILD_DIR)/FS_testclient.o: FS_testclient.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/Client.elf: $(addprefix $(BUILD_DIR)/, $(Client_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@
# Client building end

$(IMAGE_FILE) $(REPORT_FILE): $(addprefix $(BUILD_DIR)/, $(ELFS)) $(SYSTEM_DESCRIPTION) $(BLK_DRIVER_VM_DIR)
	$(MICROKIT_TOOL) $(SYSTEM_DESCRIPTION) --search-path $(BUILD_DIR) $(BLK_DRIVER_VM_DIR) --board $(BOARD) --config $(CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)