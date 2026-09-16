#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This Makefile is copied into the build directory
# and operated on from there.
#
vpath %.c ${SDDF} ${LIONSOS}/examples/pager
IMAGES := blk_driver.elf blk_virt.elf pager.elf client.elf timer_driver.elf \
eth_driver.elf \
fat.elf \
serial_driver.elf \
	serial_virt_rx.elf \
	serial_virt_tx.elf
SUPPORTED_BOARDS := \
	qemu_virt_aarch64 \
	maaxboard \
	rpi4b_1gb

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif

ifeq ($(strip $(SDDF)),)
$(error SDDF must be specified)
endif
ifeq ($(strip $(LIONSOS)),)
$(error LIONSOS must be specified)
endif

ifeq ($(strip $(MICROKIT_BOARD)), maaxboard)
	BLK_DRIV_DIR := mmc/imx
	SERIAL_DRIV_DIR := imx
	TIMER_DRIV_DIR := imx
	CPU := cortex-a53
else ifeq ($(strip $(MICROKIT_BOARD)), rpi4b_1gb)
	BLK_DRIV_DIR := virtio/mmio
	SERIAL_DRIV_DIR := ns16550a
	TIMER_DRIV_DIR := bcm2835
	CPU := cortex-a72
else ifeq ($(strip $(MICROKIT_BOARD)), qemu_virt_aarch64)
	BLK_DRIV_DIR := virtio/mmio
	SERIAL_DRIV_DIR := arm
	TIMER_DRIV_DIR := arm
	IMAGES += blk_driver.elf
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
OBJCOPY := llvm-objcopy
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
DTC := dtc
PYTHON ?= python3

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
GEN_CONFIG_H := $(BOARD_DIR)/include/kernel/gen_config.h
ifeq ($(wildcard $(GEN_CONFIG_H)),)
$(error Could not find $(GEN_CONFIG_H) -- check that MICROKIT_SDK ($(MICROKIT_SDK)) is a valid Microkit SDK containing a build for board $(MICROKIT_BOARD)/$(MICROKIT_CONFIG))
endif
ARCH := $(shell grep 'CONFIG_SEL4_ARCH  ' $(GEN_CONFIG_H) | cut -d' ' -f4)
SDDF := $(LIONSOS)/dep/sddf
LWIP := $(SDDF)/network/ipstacks/lwip/src
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco

ifeq ($(ARCH),aarch64)
	CFLAGS_ARCH := -mcpu=$(CPU)
	TARGET := aarch64-none-elf
else ifeq ($(ARCH),riscv64)
	CFLAGS_ARCH := -march=rv64imafdc
	TARGET := riscv64-none-elf
else
$(error Unsupported ARCH given)
endif

BUILD_DIR ?= build

ifeq ($(strip $(NVME)),1)
	BLK_DRIV_DIR := nvme
	QEMU_BLK_ARGS := -device nvme,drive=hd,serial=TEST1234,addr=0x4.0
endif

# Allow to user to specify a custom partition
PARTITION :=
ifdef PARTITION
	PARTITION_ARG := --partition $(PARTITION)
endif

ifeq ($(strip $(TOOLCHAIN)), clang)
	CFLAGS_ARCH += -target $(TARGET)
endif

IMAGE_FILE := loader.img
REPORT_FILE  := report.txt
SYSTEM_FILE := pager.system

TOP := ${LIONSOS}/examples/pager
CONFIGS_INCLUDE := ${TOP}
SDDF_CUSTOM_LIBC := 1
SPEC = $(BUILD_DIR)/capdl_spec.json

FAT := $(LIONSOS)/components/fs/fat

CFLAGS := \
	-mstrict-align \
	-ffreestanding \
	-O3 \
	-g3 \
	-Wall \
	-Wno-unused-function \
	-Wno-bitwise-op-parentheses \
	-Wno-shift-op-parentheses \
	-I$(BOARD_DIR)/include \
	$(CFLAGS_ARCH) \
	-DBOARD_$(MICROKIT_BOARD) \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(LIBMICROKITCO_PATH) \
	-I$(TOP) \
	-I$(TOP)/benchmarks/519.lbm_r/src \
	-I$(TOP)/benchmarks/minor_page_fault_latency \
	-I$(TOP)/include
include $(LIONSOS)/lib/libc/libc.mk
include $(SDDF)/tools/make/board/common.mk
LDFLAGS := -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib -L$(TOP)/benchmarks/519.lbm_r/src -L$(TOP)/benchmarks/minor_page_fault_latency
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a
CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@

METAPROGRAM := $(TOP)/meta.py
BLK_DRIVER := $(SDDF)/drivers/blk/${BLK_DRIV_DIR}
BLK_COMPONENTS := $(SDDF)/blk/components

all: $(IMAGE_FILE)

SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include
include ${SDDF}/util/util.mk
include ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk
include ${SDDF}/drivers/serial/${SERIAL_DRIV_DIR}/serial_driver.mk
include ${SDDF}/serial/components/serial_components.mk
include ${SDDF}/network/lib_sddf_lwip/lib_sddf_lwip.mk
include ${SDDF}/libco/libco.mk
include ${BLK_DRIVER}/blk_driver.mk
include ${BLK_COMPONENTS}/blk_components.mk
include $(SDDF)/drivers/network/$(NET_DRIV_DIR)/eth_driver.mk
FAT_LIBC_LIB := $(LIONS_LIBC)/lib/libc.a
FAT_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIONSOS)/components/fs/fat/fat.mk
LIBMICROKITCO_CFLAGS_client := -O3 -I$(TOP)
LIBMICROKITCO_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIBMICROKITCO_PATH)/libmicrokitco.mk

${IMAGES}: $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a 519.a minor_pf.a

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

PAGER_OBJS := bitmap.o cspace.o frame_table.o mem.o page_table.o pager.o \
	proc.o untyped.o

$(PAGER_OBJS) client.o: %.o: $(TOP)/src/%.c $(TOP)/include | $(LIONS_LIBC)/include
	$(CC) -c $(CFLAGS) -I. $< -o $@

pager.elf: $(PAGER_OBJS) libsddf_util_debug.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

client.elf: client.o libsddf_util_debug.a libmicrokitco_client.a 519.a minor_pf.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@
519.a:
	$(MAKE) -C $(TOP)/benchmarks/519.lbm_r/src
minor_pf.a:
	$(MAKE) -C $(TOP)/benchmarks/minor_page_fault_latency

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	PYTHONPATH=${SDDF}/tools/meta:$$PYTHONPATH \
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE)

	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .serial_virt_rx_config=serial_virt_rx.data serial_virt_rx.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .device_resources=blk_driver_device_resources.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_driver_config=blk_driver.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_virt_config=blk_virt.data blk_virt.elf
	$(OBJCOPY) --update-section .blk_client_config=blk_client_fatfs.data fat.elf
	$(OBJCOPY) --update-section .fs_server_config=fs_server_fatfs.data fat.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_client.data client.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_client.data client.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_client.data client.elf

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

# if using actual storage
qemu: ${IMAGE_FILE}
	$(QEMU) -machine virt,virtualization=on \
		-cpu cortex-a53 \
		-serial mon:stdio \
		-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
		-m size=2G \
		-nographic \
		-global virtio-mmio.force-legacy=false \
		-d guest_errors \
		-drive file=/dev/mmcblk0,if=none,format=raw,id=hd \
		-device virtio-blk-device,drive=hd,bus=virtio-mmio-bus.1
