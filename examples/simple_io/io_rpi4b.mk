#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

vpath %.c ${SDDF} ${LIONSOS}/examples/io_rpi4b
IMAGES := client.elf \
	serial_driver.elf \
	serial_virt_tx.elf
SUPPORTED_BOARDS := \
	rpi4b_1gb \
	qemu_virt_aarch64
ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
ifeq ($(strip $(SDDF)),)
$(error SDDF must be specified)
endif
ifeq ($(strip $(LIONSOS)),)
$(error LIONSOS must be specified)
endif
ifeq ($(strip $(MICROKIT_BOARD)), rpi4b_1gb)
	SERIAL_DRIV_DIR := ns16550a
	CPU := cortex-a72
else ifeq ($(strip $(MICROKIT_BOARD)), qemu_virt_aarch64)
	SERIAL_DRIV_DIR := arm
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
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco
ifeq ($(ARCH),aarch64)
	CFLAGS_ARCH := -mcpu=$(CPU)
	TARGET := aarch64-none-elf
else
$(error Unsupported ARCH given)
endif

BUILD_DIR ?= build

ifeq ($(strip $(TOOLCHAIN)), clang)
	CFLAGS_ARCH += -target $(TARGET)
endif

IMAGE_FILE := loader.img
REPORT_FILE  := report.txt
SYSTEM_FILE := io_rpi4b.system

TOP := ${LIONSOS}/examples/io_rpi4b
CONFIGS_INCLUDE := ${TOP}
SDDF_CUSTOM_LIBC := 1

# simple_io is the only benchmark here. It needs a filesystem and a
# serial console and nothing else, which is why this example has no pager PD:
# the client is an ordinary statically-mapped Microkit PD and libc's heap is the
# static arena it passes to libc_init(). Only client.c needs the benchmark
# header, so it stays off everything else's include path.
BENCHMARKS_DIR := $(LIONSOS)/dep/benchmarking/lionsos
SIMPLE_IO_DIR := $(BENCHMARKS_DIR)/simple_io
BENCHMARK_LIBS := simple_io.a
BENCHMARK_INCLUDES := -I$(SIMPLE_IO_DIR)
$(if $(wildcard $(SIMPLE_IO_DIR)/Makefile),,$(error Could not find a benchmark Makefile in $(SIMPLE_IO_DIR) -- run `make submodules`))

# RAM_FATFS in the ramfs config moves the client's files from file.c (which has
# no file system PD to talk to here) to FatFs over a RAM disk inside the client.
# The disk starts out as a cpio archive of the RAMFS_ROOTS merged together.
RAMFS_CONFIG_DIR := $(LIONSOS)/components/fs/ramfs/config
HASH := \#
RAM_FATFS := $(shell grep -qE '^[[:space:]]*$(HASH)[[:space:]]*define[[:space:]]+RAM_FATFS([[:space:]]|$$)' $(RAMFS_CONFIG_DIR)/fat_config.h && echo 1)
# simple_io reads no input files -- it creates its own 4 KiB file at runtime --
# so the volume starts out empty and an empty cpio is what it wants.
RAMFS_ROOTS ?=
# 32 MiB: the volume is formatted with 4 KiB clusters and FM_FAT|FM_FAT32|FM_SFD,
# and this puts the cluster count clear of the FAT12/FAT16 boundary while leaving
# plenty of room above simple_io's own 4 KiB file.
RAMFS_SIZE ?= 0x2000000
RAMFS_STAGE := ramfs_root
RAMFS_EXCLUDE := control

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
	-I$(TOP)
include $(LIONSOS)/lib/libc/libc.mk
include $(SDDF)/tools/make/board/common.mk
LDFLAGS := -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a
CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@

METAPROGRAM := $(TOP)/meta.py

all: $(IMAGE_FILE)

SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include
include ${SDDF}/util/util.mk
include ${SDDF}/drivers/serial/${SERIAL_DRIV_DIR}/serial_driver.mk
include ${SDDF}/serial/components/serial_components.mk
LIBMICROKITCO_CFLAGS_client := -O3 -I$(TOP)
LIBMICROKITCO_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIBMICROKITCO_PATH)/libmicrokitco.mk
ifeq ($(RAM_FATFS),1)
RAMFS_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIONSOS)/components/fs/ramfs/ramfs.mk
RAMFS_LIB := ramfs.a
RAMFS_IMAGE := ramfs.cpio
META_RAMFS_ARGS := --ramfs-image $(RAMFS_IMAGE) --ramfs-size $(RAMFS_SIZE)
endif

${IMAGES}: $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a $(BENCHMARK_LIBS)

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

client.o: %.o: $(TOP)/src/%.c | $(LIONS_LIBC)/include
	$(CC) -c $(CFLAGS) -I. -I$(RAMFS_CONFIG_DIR) $(BENCHMARK_INCLUDES) $< -o $@

client.elf: client.o libsddf_util_debug.a libmicrokitco_client.a $(BENCHMARK_LIBS) $(RAMFS_LIB)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

ifeq ($(RAM_FATFS),1)
# io.c unpacks this into a FAT volume when the client mounts it. Entry names
# must not start with "./". cpio names its entries relative to a single
# directory, so the input trees are staged into one first; a missing root is a
# warning rather than an error, and no roots at all -- the default here -- gives
# an empty file system. The $(if) guards the prerequisite scan: bare `find` with
# no path walks the build directory and would make this depend on its own output.
$(RAMFS_IMAGE): $(if $(strip $(RAMFS_ROOTS)),$(shell find $(RAMFS_ROOTS) 2>/dev/null))
	rm -rf $(RAMFS_STAGE)
	mkdir -p $(RAMFS_STAGE)
	for root in $(RAMFS_ROOTS); do \
		if [ -d "$$root" ]; then \
			cp -R "$$root"/. $(RAMFS_STAGE)/; \
		else \
			echo "WARNING: ramfs input $$root not found, skipping"; \
		fi; \
	done
	$(foreach e,$(RAMFS_EXCLUDE),rm -rf $(RAMFS_STAGE)/$(e);)
	(cd $(RAMFS_STAGE) && find . -mindepth 1 -printf '%P\n' | LC_ALL=C sort | cpio -o -H newc --quiet) > $@

clean::
	rm -rf $(RAMFS_STAGE)
endif

-include client.d

.PHONY: $(BENCHMARK_LIBS)
# The benchmark includes libc headers, which only exist once musl is installed.
simple_io.a: | $(LIONS_LIBC)/include
	$(MAKE) -C $(SIMPLE_IO_DIR) CPU=$(CPU) LIONS_LIBC=$(LIONS_LIBC)

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB) $(RAMFS_CONFIG_DIR)/fat_config.h $(RAMFS_IMAGE)
	PYTHONPATH=${SDDF}/tools/meta:$$PYTHONPATH \
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE) $(META_RAMFS_ARGS)

	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_client.data client.elf

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

qemu: ${IMAGE_FILE}
	$(QEMU) -machine virt,virtualization=on \
		-cpu cortex-a53 \
		-serial mon:stdio \
		-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
		-m size=2G \
		-nographic \
		-d guest_errors
