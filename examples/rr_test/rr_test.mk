#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
SUPPORTED_BOARDS := \
	qemu_virt_aarch64 \
	maaxboard

IMAGES := \
	debugger.elf \
	faulter.elf

TOOLCHAIN ?= clang
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
export BOARD := $(MICROKIT_BOARD)
SDDF := $(LIONSOS)/dep/sddf
SYSTEM_FILE := rr_test.system
IMAGE_FILE := rr_test.img
REPORT_FILE := report.txt
SERIAL_DRIV_DIR := virtio
SERIAL_COMPONENTS := $(SDDF)/serial/components
SERIAL_DRIVER := $(SDDF)/drivers/serial/$(SERIAL_DRIV_DIR)

LIBGDB_DIR=$(LIONSOS)/dep/libgdb
LIBVSPACE_DIR=$(LIBGDB_DIR)/libvspace


all: ${IMAGE_FILE}

include ${SDDF}/tools/make/board/common.mk

METAPROGRAM := $(RR_TEST_DIR)/meta.py
DEBUGGER_DIR := $(RR_TEST_DIR)/debugger

CFLAGS += \
	-DMICROKIT \
	-Wno-bitwise-op-parentheses \
	-Wno-shift-op-parentheses \
	-Wno-unused-function \
	-Wno-tautological-constant-out-of-range-compare \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/libco \
	-I$(SDDF)/include/microkit \
	-I$(LWIP)/include \
	-DMAX_FDS=8 \
	-I$(LIBGDB_DIR)/include \
	-I$(LIBGDB_DIR)/arch_include \
	-I$(LIBVSPACE_DIR) \
	-I${DEBUGGER_INCLUDE}/lwip \
	-funwind-tables -O0 -ggdb

include $(LIONSOS)/lib/libc/libc.mk

LDFLAGS := --eh-frame-hdr -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib -L$(RR_TEST_DIR)/build
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a -lc libvspace.a

SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include

include ${SDDF}/util/util.mk
include ${SDDF}/libco/libco.mk
include ${SERIAL_DRIVER}/serial_driver.mk
include ${SERIAL_COMPONENTS}/serial_components.mk
include $(LIBGDB_DIR)/libgdb.mk
include $(LIBVSPACE_DIR)/libvspace.mk
include $(DEBUGGER_DIR)/debugger.mk

${IMAGES}: $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a libvspace.a

faulter.o: $(RR_TEST_DIR)/faulter.c | $(LIONS_LIBC)/include
	${CC} ${CFLAGS} -c -o $@ $<

faulter.elf: faulter.o
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

FORCE:

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	PYTHONPATH="${SDDF}/tools/meta:$$PYTHONPATH:$(PYTHONPATH)" $(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --output . --sdf $(SYSTEM_FILE)
	# Add the unwind table to the memory region specified.

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

qemu_disk:
	$(SDDF)/tools/mkvirtdisk $@ 1 512 16777216 GPT

qemu: ${IMAGE_FILE} qemu_disk
	$(QEMU) -machine virt,virtualization=on \
		-cpu cortex-a53 \
		-serial mon:stdio \
		-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
		-m size=2G \
		-nographic \
		-global virtio-mmio.force-legacy=false \
		-d guest_errors \
		-drive file=qemu_disk,if=none,format=raw,id=hd \
		-device virtio-blk-device,drive=hd,bus=virtio-mmio-bus.1 \
		-device virtio-net-device,netdev=netdev0,bus=virtio-mmio-bus.0 \
		-netdev user,id=netdev0,hostfwd=tcp::5560-10.0.2.15:5560,hostfwd=tcp::5561-10.0.2.15:5561

clean::
	${RM} -rf ${IMAGES} faulter.o faulter.elf
