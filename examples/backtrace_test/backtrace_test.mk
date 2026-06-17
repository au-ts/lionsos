#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
SUPPORTED_BOARDS := \
	qemu_virt_aarch64 \
	maaxboard

IMAGES := \
	faulter.elf

TOOLCHAIN ?= clang
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
SDDF := $(LIONSOS)/dep/sddf
LLVM := $(LIONSOS)/dep/llvm-project/
SYSTEM_FILE := backtrace_test.system
IMAGE_FILE := backtrace_test.img
REPORT_FILE := report.txt
BACKTRACER := $(LIONSOS)/components/backtracer

all: ${IMAGE_FILE}

include ${SDDF}/tools/make/board/common.mk

METAPROGRAM := $(BACKTRACE_TEST_DIR)/meta.py

FAT := $(LIONSOS)/components/fs/fat

CFLAGS += \
	-Wno-bitwise-op-parentheses \
	-Wno-shift-op-parentheses \
	-Wno-unused-function \
	-Wno-tautological-constant-out-of-range-compare \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(LWIP)/include \
	-I$(LIBUNWIND)/include \
	-DMAX_FDS=8 \
	-funwind-tables -O0 -ggdb

include $(LIONSOS)/lib/libc/libc.mk

LDFLAGS := --eh-frame-hdr -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib -L$(BACKTRACE_TEST_DIR)/build -L$(BACKTRACER)
LIBS := --start-group -Tunwind.ld -lmicrokit -Tmicrokit.ld libsddf_util_debug.a -lc -lunwind --end-group

SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include
include ${SDDF}/util/util.mk

FAT_LIBC_LIB := $(LIONS_LIBC)/lib/libc.a
FAT_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIONSOS)/components/fs/fat/fat.mk

include $(BACKTRACER)/backtracer.mk

${IMAGES}: $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a

faulter.o: $(BACKTRACE_TEST_DIR)/faulter.c | $(LIONS_LIBC)/include
	${CC} ${CFLAGS} -c -o $@ $<

faulter.elf: faulter.o libunwind.a unwind_helpers.o
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

FORCE:

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB) backtracer.elf
	PYTHONPATH="${SDDF}/tools/meta:${BACKTRACER}:$$PYTHONPATH:$(PYTHONPATH)" $(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --output . --sdf $(SYSTEM_FILE)
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
