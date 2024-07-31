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
TOP := $(LIONSOS)/examples/webserver
ifeq (${MICROKIT_BOARD},odroidc4)
PLATFORM := meson
CPU := cortex-a55
endif

TOOLCHAIN := clang
CC := clang
LD := ld.lld
AR := llvm-ar
RANLIB := llvm-ranlib
TARGET := aarch64-none-elf
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
CP := /usr/bin/cp

LWIP=$(SDDF)/network/ipstacks/lwip/src
LIBNFS=$(LIONSOS)/dep/libnfs
NFS=$(LIONSOS)/components/fs/nfs
MUSL=$(LIONSOS)/dep/musllibc
BUILD_DIR=$(abspath .)

IMAGES := timer_driver.elf eth_driver.elf micropython.elf nfs.elf \
	  copy.elf network_virt_rx.elf network_virt_tx.elf \
	  uart_driver.elf serial_virt_rx.elf serial_virt_tx.elf

CFLAGS := \
	-mtune=$(CPU) \
	-mstrict-align \
	-ffreestanding \
	-O3 \
	-Wall \
	-Wno-unused-function \
	-I$(BOARD_DIR)/include \
	-target $(TARGET) \
	-DBOARD_$(MICROKIT_BOARD) \
	-I$(SDDF)/include \
	-I$(CONFIG_INCLUDE)

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a

IMAGE_FILE := webserver.img
REPORT_FILE := report.txt

#.EXTRA_PREREQS := submodules

all: $(IMAGE_FILE)
${IMAGES}: libsddf_util_debug.a

CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@

micropython.elf: mpy-cross ${TOP}/manifest.py ${TOP}/webserver.py config.py
	make -C $(LIONSOS)/components/micropython -j$(nproc) \
			MICROKIT_SDK=$(MICROKIT_SDK) \
			MICROKIT_BOARD=$(MICROKIT_BOARD) \
			MICROKIT_CONFIG=$(MICROKIT_CONFIG) \
			BUILD=$(abspath $(BUILD_DIR)) \
			LIBMATH=$(abspath $(BUILD_DIR)/libm) \
			CONFIG_INCLUDE=$(abspath $(CONFIG_INCLUDE)) \
			FROZEN_MANIFEST=$(abspath ${TOP}/manifest.py) \
			EXEC_MODULE="webserver.py"

config.py: ${CHECK_FLAGS_BOARD_MD5}
	echo "base_dir='$(WEBSITE_DIR)'" > config.py

%.py: ${WEBSERVER_SRC_DIR}/%.py
	${CP} $< $@

musllibc/lib/libc.a:
	make -C $(MUSL) \
		C_COMPILER=aarch64-none-elf-gcc \
		TOOLPREFIX=aarch64-none-elf- \
		CONFIG_ARCH_AARCH64=y \
		STAGE_DIR=$(abspath ./musllibc) \
		SOURCE_DIR=.

libnfs/lib/libnfs.a: musllibc/lib/libc.a
	MUSL=$(abspath musllibc) cmake -S $(LIBNFS) -B libnfs
	cmake --build libnfs

nfs/nfs.a: musllibc/lib/libc.a FORCE
	make -C $(NFS) \
		BUILD_DIR=$(abspath nfs) \
		MICROKIT_INCLUDE=$(BOARD_DIR)/include \
		MUSLLIBC_INCLUDE=$(abspath musllibc/include) \
		LIBNFS_INCLUDE=$(abspath $(LIBNFS)/include) \
		CONFIG_INCLUDE=$(abspath $(CONFIG_INCLUDE))

nfs.elf: nfs/nfs.a libnfs/lib/libnfs.a musllibc/lib/libc.a
	$(LD) \
		$(LDFLAGS) \
		nfs/nfs.a \
		-Llibnfs/lib \
		-Lmusllibc/lib \
		-L$(LIBGCC) \
		-lgcc \
		-lc \
		$(LIBS) \
		-lnfs \
		-o nfs.elf

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

include ${SDDF}/util/util.mk
include ${SDDF}/drivers/clock/${PLATFORM}/timer_driver.mk
include ${SDDF}/drivers/network/${PLATFORM}/eth_driver.mk
include ${SDDF}/drivers/serial/${PLATFORM}/uart_driver.mk
include ${SDDF}/network/components/network_components.mk
include ${SDDF}/serial/components/serial_components.mk

$(IMAGE_FILE) $(REPORT_FILE):  $(IMAGES) $(TOP)/webserver.system
	$(MICROKIT_TOOL) $(TOP)/webserver.system --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

FORCE: ;

mpy-cross: FORCE
	make -C $(LIONSOS)/dep/micropython/mpy-cross

.PHONY: mpy-cross submodules
