#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
ifeq ($(strip $(MICROKIT_BOARD)), odroidc4)
    NET_DRIV_DIR := meson
    UART_DRIV_DIR := meson
    TIMER_DRIV_DIR := meson
    CPU := cortex-a55
else ifeq ($(strip $(MICROKIT_BOARD)), qemu_virt_aarch64)
    NET_DRIV_DIR := virtio
    BLK_DRIV_DIR := virtio
    UART_DRIV_DIR := arm
    TIMER_DRIV_DIR := arm
    CPU := cortex-a53
    QEMU := qemu-system-aarch64
else
$(error Unsupported MICROKIT_BOARD given)
endif

TOOLCHAIN := clang
CC := clang
LD := ld.lld
RANLIB := llvm-ranlib
AR := llvm-ar
TARGET := aarch64-none-elf
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
DTC := dtc

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
PLATFORM := meson
SDDF := $(LIONSOS)/dep/sddf
LIBVMM_DIR := $(LIONSOS)/dep/libvmm

VMM_IMAGE_DIR := ${FILEIO_DIR}/src/vmm/images
LINUX := 90c4247bcd24cbca1a3db4b7489a835ce87a486e-linux
INITRD := 08c10529dc2806559d5c4b7175686a8206e10494-rootfs.cpio.gz
DTS := $(VMM_IMAGE_DIR)/linux.dts
DTB := linux.dtb

LWIP := $(SDDF)/network/ipstacks/lwip/src
FATFS := $(LIONSOS)/components/fs/fat
MUSL := $(LIONSOS)/dep/musllibc

IMAGES := timer_driver.elf \
	  eth_driver.elf \
	  micropython.elf \
	  fatfs.elf \
	  copy.elf \
	  network_virt_rx.elf \
	  network_virt_tx.elf \
	  uart_driver.elf \
	  serial_virt_rx.elf \
	  serial_virt_tx.elf \
	  blk_virt.elf \
	  blk_driver.elf

CFLAGS := \
	-mtune=$(CPU) \
	-mstrict-align \
	-ffreestanding \
	-g \
	-O0 \
	-Wall \
	-Wno-unused-function \
	-I$(BOARD_DIR)/include \
	-target $(TARGET) \
	-DBOARD_$(MICROKIT_BOARD) \
	-I$(SDDF)/include \
	-I${CONFIG_INCLUDE} \
	-DVIRTIO_MMIO_NET_OFFSET=0xc00


LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a

IMAGE_FILE := fileio.img
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
include ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk
include ${SDDF}/drivers/network/${NET_DRIV_DIR}/eth_driver.mk
include ${SDDF}/drivers/serial/${UART_DRIV_DIR}/uart_driver.mk
include ${SDDF}/network/components/network_components.mk
include ${SDDF}/serial/components/serial_components.mk
include ${SDDF}/libco/libco.mk
include ${BLK_DRIVER}/blk_driver.mk
include ${BLK_COMPONENTS}/blk_components.mk

# Build with two threads in parallel
# nproc=2

micropython.elf: mpy-cross libsddf_util_debug.a libco.a
	cp $(LIONSOS)/components/fs/fat/fs_test.py .
	cp $(LIONSOS)/examples/fileio/manifest.py .
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

fatfs.elf: musllibc/lib/libc.a
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
		   TARGET=$(TARGET) \
		   USE_LIBMICROKITCO=1

musllibc/lib/libc.a:
	make -C $(MUSL) \
		C_COMPILER=aarch64-none-elf-gcc \
		TOOLPREFIX=aarch64-none-elf- \
		CONFIG_ARCH_AARCH64=y \
		STAGE_DIR=$(abspath $(BUILD_DIR)/musllibc) \
		SOURCE_DIR=.

${IMAGES}: libsddf_util_debug.a

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) ${FILEIO_DIR}/board/$(MICROKIT_BOARD)/fileio.system
	$(MICROKIT_TOOL) ${FILEIO_DIR}/board/$(MICROKIT_BOARD)/fileio.system --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

FORCE:

%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

mpy-cross: FORCE
	${MAKE} -C ${LIONSOS}/dep/micropython/mpy-cross BUILD=$(abspath ./mpy_cross)

qemu_disk: mydisk

mydisk:
	$(LIONSOS)/dep/sddf/examples/blk/mkvirtdisk mydisk 1 512 16777216

qemu: ${IMAGE_FILE} qemu_disk
	$(QEMU) -machine virt,virtualization=on \
			-cpu cortex-a53 \
			-serial mon:stdio \
            -device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
            -m size=2G \
            -nographic \
            -global virtio-mmio.force-legacy=false \
            -d guest_errors \
            -drive file=mydisk,if=none,format=raw,id=hd \
            -device virtio-blk-device,drive=hd \
			-device virtio-net-device,netdev=netdev0 \
            -netdev user,id=netdev0
