#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

TOOLCHAIN ?= clang
SUPPORTED_BOARDS := \
	qemu_virt_aarch64 \
	maaxboard

IMAGES := \
	faulter.elf \
	backtracer.elf

TOOLCHAIN ?= clang
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
SDDF := $(LIONSOS)/dep/sddf
LWIP := $(SDDF)/network/ipstacks/lwip/src
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco
LIBUNWIND := $(LIONSOS)/dep/llvm-project/libunwind
SYSTEM_FILE := posix_test.system
IMAGE_FILE := posix_test.img
REPORT_FILE := report.txt

all: ${IMAGE_FILE}

include ${SDDF}/tools/make/board/common.mk

METAPROGRAM := $(POSIX_TEST_DIR)/meta.py

FAT := $(LIONSOS)/components/fs/fat

CFLAGS += \
	-Wno-bitwise-op-parentheses \
	-Wno-shift-op-parentheses \
	-Wno-unused-function \
	-Wno-tautological-constant-out-of-range-compare \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(LIBMICROKITCO_PATH) \
	-I$(LWIP)/include \
	-I$(LIBUNWIND)/include \
	-DMAX_FDS=8 \
	-funwind-tables

include $(LIONSOS)/lib/libc/libc.mk

LDFLAGS := --eh-frame-hdr -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib -L$(POSIX_TEST_DIR)/build
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a -lc -T$(POSIX_TEST_DIR)/unwind.ld -lunwind

BLK_DRIVER := $(SDDF)/drivers/blk/${BLK_DRIV_DIR}
BLK_COMPONENTS := $(SDDF)/blk/components

SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include
include ${SDDF}/util/util.mk
include ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk
include ${SDDF}/drivers/serial/${UART_DRIV_DIR}/serial_driver.mk
include ${SDDF}/drivers/network/${NET_DRIV_DIR}/eth_driver.mk
include ${SDDF}/serial/components/serial_components.mk
include ${SDDF}/network/components/network_components.mk

LIB_SDDF_LWIP_CFLAGS := -I${POSIX_TEST_DIR}/lwip_include
include ${SDDF}/network/lib_sddf_lwip/lib_sddf_lwip.mk

include ${SDDF}/libco/libco.mk
include ${BLK_DRIVER}/blk_driver.mk
include ${BLK_COMPONENTS}/blk_components.mk

FAT_LIBC_LIB := $(LIONS_LIBC)/lib/libc.a
FAT_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIONSOS)/components/fs/fat/fat.mk

LIBMICROKITCO_CFLAGS_posix_test := -I$(POSIX_TEST_DIR)
LIBMICROKITCO_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIBMICROKITCO_PATH)/libmicrokitco.mk

${IMAGES}: $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a

# for libmicrokitco_opts.h and lwipopts.h
backtracer.o: $(POSIX_TEST_DIR)/backtracer.c | $(LIONS_LIBC)/include
	${CC} ${CFLAGS} -c -o $@ $<

backtracer.elf: backtracer.o libunwind.a
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

faulter.o: $(POSIX_TEST_DIR)/faulter.c | $(LIONS_LIBC)/include
	${CC} ${CFLAGS} -c -o $@ $<

faulter.elf: faulter.o libunwind.a
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

FORCE:

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	PYTHONPATH=${SDDF}/tools/meta:$$PYTHONPATH $(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --output . --sdf $(SYSTEM_FILE)
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
		-netdev user,id=netdev0,hostfwd=tcp::5560-10.0.2.15:5560,hostfwd=tcp::5561-10.0.2.15:5561 \

libunwind.a:
	echo "Please paste libunwind.a in here!"
