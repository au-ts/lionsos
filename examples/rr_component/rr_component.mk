#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
SUPPORTED_BOARDS := \
	qemu_virt_aarch64 \
	maaxboard

IMAGES := \
	ping.elf \
	pong.elf \
	rrer.elf

TOOLCHAIN ?= clang
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
export BOARD := $(MICROKIT_BOARD)

DTB := $(MICROKIT_BOARD).dtb
DTS := $(SDDF)/dts/$(MICROKIT_BOARD).dts

SDDF := $(LIONSOS)/dep/sddf
SYSTEM_FILE := rr_component.system
IMAGE_FILE := rr_component.img
REPORT_FILE := report.txt

include ${SDDF}/tools/make/board/common.mk

LIBGDB_DIR=$(LIONSOS)/dep/libgdb
LIBVSPACE_DIR=$(LIBGDB_DIR)/libvspace

METAPROGRAM := $(RR_COMPONENT_DIR)/meta.py
DEBUGGER_DIR := $(LIONSOS)/components/debugger


CFLAGS += \
	-DMICROKIT \
	-Wno-bitwise-op-parentheses \
	-Wno-shift-op-parentheses \
	-Wno-unused-function \
	-Wno-tautological-constant-out-of-range-compare \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-DMAX_FDS=8 \
	-I$(LIBGDB_DIR)/include \
	-I$(LIBGDB_DIR)/arch_include \
	-I$(LIBVSPACE_DIR) \
	-ggdb

include $(LIONSOS)/lib/libc/libc.mk

QEMU_ARGS := -machine virt,virtualization=on \
		-cpu cortex-a53 \
		-serial mon:stdio \
		-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
		-m size=2G \
		-nographic \
		-global virtio-mmio.force-legacy=false \
		-d guest_errors \
		-device virtio-serial-device \
		-chardev pty,id=virtcon \
		-device virtconsole,chardev=virtcon #-S -s


LDFLAGS := -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib -L$(RR_COMPONENT_DIR)/build
LIBS := -lmicrokit -Tmicrokit.ld -lc

SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include
include ${SDDF}/util/util.mk
include ${SDDF}/libco/libco.mk

include $(RR_COMPONENT_DIR)/rrer/rrer.mk


all: ${IMAGE_FILE}

${IMAGES}: $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a

ping.o: $(RR_COMPONENT_DIR)/ping.c | $(LIONS_LIBC)/include
	@echo "$(CFLAGS)"
	${CC} ${CFLAGS} -c -o $@ $<

pong.o: $(RR_COMPONENT_DIR)/pong.c | $(LIONS_LIBC)/include
	${CC} ${CFLAGS} -c -o $@ $<

ping.elf: ping.o
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

pong.elf: pong.o
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

FORCE:

$(DTB): $(DTS)
	dtc -q -I dts -O dtb $(DTS) > $(DTB)


$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	$(PYTHON) $(METAPROGRAM) \
		--output . --sdf $(SYSTEM_FILE)

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

qemu_disk:
	$(SDDF)/tools/mkvirtdisk $@ 1 512 16777216 GPT

qemu: ${IMAGE_FILE} qemu_disk
	$(QEMU) $(QEMU_ARGS)

clean::
	${RM} -rf ${IMAGES} ping.elf pong.elf
