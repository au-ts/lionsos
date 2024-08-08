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

NFS=$(LIONSOS)/components/fs/nfs

IMAGES := timer_driver.elf eth_driver.elf micropython.elf nfs.elf \
	  copy.elf network_virt_rx.elf network_virt_tx.elf \
	  uart_driver.elf serial_virt_tx.elf

CFLAGS := \
	-mtune=$(CPU) \
	-mstrict-align \
	-ffreestanding \
	-O3 \
	-MD \
	-MP \
	-Wall \
	-Wno-unused-function \
	-I$(BOARD_DIR)/include \
	-target $(TARGET) \
	-DBOARD_$(MICROKIT_BOARD) \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(CONFIG_INCLUDE)

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a

IMAGE_FILE := webserver.img
REPORT_FILE := report.txt

all: $(IMAGE_FILE)
${IMAGES}: libsddf_util_debug.a

CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@

micropython.elf: mpy-cross manifest.py webserver.py config.py
	make -C $(LIONSOS)/components/micropython -j$(nproc) \
			MICROKIT_SDK=$(MICROKIT_SDK) \
			MICROKIT_BOARD=$(MICROKIT_BOARD) \
			MICROKIT_CONFIG=$(MICROKIT_CONFIG) \
			BUILD=$(abspath $(BUILD_DIR)) \
			LIBMATH=$(abspath $(BUILD_DIR)/libm) \
			CONFIG_INCLUDE=$(abspath $(CONFIG_INCLUDE)) \
			FROZEN_MANIFEST=$(abspath manifest.py) \
			EXEC_MODULE="webserver.py"

config.py: ${CHECK_FLAGS_BOARD_MD5}
	echo "base_dir='$(WEBSITE_DIR)'" > config.py

%.py: ${WEBSERVER_SRC_DIR}/%.py
	cp $< $@

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

include ${SDDF}/util/util.mk
include ${SDDF}/drivers/clock/${PLATFORM}/timer_driver.mk
include ${SDDF}/drivers/network/${PLATFORM}/eth_driver.mk
include ${SDDF}/drivers/serial/${PLATFORM}/uart_driver.mk
include ${SDDF}/network/components/network_components.mk
include ${SDDF}/serial/components/serial_components.mk
include $(NFS)/nfs.mk

$(IMAGE_FILE) $(REPORT_FILE):  $(IMAGES) $(TOP)/webserver.system
	$(MICROKIT_TOOL) $(TOP)/webserver.system --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

FORCE: ;

mpy-cross: FORCE
	make -C $(LIONSOS)/dep/micropython/mpy-cross

.PHONY: mpy-cross submodules
