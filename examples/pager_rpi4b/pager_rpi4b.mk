#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

vpath %.c ${SDDF} ${LIONSOS}/examples/pager_rpi4b
IMAGES := pager.elf client.elf \
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
SYSTEM_FILE := pager_rpi4b.system

TOP := ${LIONSOS}/examples/pager_rpi4b
CONFIGS_INCLUDE := ${TOP}
SDDF_CUSTOM_LIBC := 1

# The client only ever needs the frames the WRITE pass allocates; retyping the
# default 200000 at init does not fit in the Pi's 1GiB.
PAGER_INIT_FRAMES := 40000

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
	-DINIT_FRAMES=$(PAGER_INIT_FRAMES) \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(LIBMICROKITCO_PATH) \
	-I$(TOP) \
	-I$(TOP)/benchmarks/minor_page_fault_latency
include $(LIONSOS)/lib/libc/libc.mk
include $(SDDF)/tools/make/board/common.mk
LDFLAGS := -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib -L$(TOP)/benchmarks/minor_page_fault_latency
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
include $(LIONSOS)/components/pager/pager.mk
# musl has to be configured before the pager, which includes its headers.
$(PAGER_OBJ): | $(LIONS_LIBC)/include
LIBMICROKITCO_CFLAGS_client := -O3 -I$(TOP)
LIBMICROKITCO_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIBMICROKITCO_PATH)/libmicrokitco.mk

${IMAGES}: $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a minor_pf.a

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

client.o: %.o: $(TOP)/src/%.c | $(LIONS_LIBC)/include
	$(CC) -c $(CFLAGS) -I. $< -o $@


client.elf: client.o libsddf_util_debug.a libmicrokitco_client.a minor_pf.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

.PHONY: minor_pf.a
minor_pf.a:
	$(MAKE) -C $(TOP)/benchmarks/minor_page_fault_latency CPU=$(CPU) LIONS_LIBC=$(LIONS_LIBC)

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	PYTHONPATH=${SDDF}/tools/meta:$$PYTHONPATH \
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE)

	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_client.data client.elf
	$(OBJCOPY) --update-section .pager_server_config=pager_server_pager.data pager.elf
	$(OBJCOPY) --update-section .pager_client_config=pager_client_client.data client.elf

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
