#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
SUPPORTED_BOARDS := \
	qemu_virt_aarch64 \
	maaxboard

IMAGES := \
	controller.elf \
	ping.elf \
	pong.elf

TOOLCHAIN ?= clang
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
export BOARD := $(MICROKIT_BOARD)

DTB := $(MICROKIT_BOARD).dtb
DTS := $(SDDF)/dts/$(MICROKIT_BOARD).dts

SDDF := $(LIONSOS)/dep/sddf
SYSTEM_FILE := pdcontrol.system
IMAGE_FILE := pdcontrol.img
REPORT_FILE := report.txt

include ${SDDF}/tools/make/board/common.mk

METAPROGRAM := $(TOP)/meta.py

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
	-DMAX_FDS=8 \
	-ggdb

QEMU_ARGS := -machine virt,virtualization=on \
		-cpu cortex-a53 \
		-serial mon:stdio \
		-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
		-m size=2G \
		-nographic \
		-global virtio-mmio.force-legacy=false \
		-d guest_errors

include $(LIONSOS)/lib/libc/libc.mk
include $(LIONSOS)/components/pdcontrol/pdcontrol.mk

LDFLAGS := -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib -L$(TOP)/build -Tmicrokit.ld
LIBS := -lmicrokit libsddf_util_debug.a -lc


SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include
include ${SDDF}/util/util.mk
include ${SDDF}/libco/libco.mk

all: ${IMAGE_FILE}

controller.o: $(TOP)/controller.c libsddf_util_debug.a | $(LIONS_LIBC)/include
	${CC} ${CFLAGS} -c -o $@ $<

controller.elf: controller.o $(LIONS_LIBC)/lib/libc.a libpdcontrol.a
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

ping.o: $(TOP)/ping.c
	${CC} ${CFLAGS} -c -o $@ $<

ping.elf: ping.o
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

pong.o: $(TOP)/pong.c
	${CC} ${CFLAGS} -c -o $@ $<

pong.elf: pong.o
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

FORCE:

$(DTB): $(DTS)
	dtc -q -I dts -O dtb $(DTS) > $(DTB)


$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB) FORCE
	PYTHONPATH="${PDC_PYPATH}:$(SDDF)" $(PYTHON) $(METAPROGRAM) \
		--output . --sdf $(SYSTEM_FILE) --dtb $(DTB)
	${OBJCOPY} --update-section .pdc_pd_all_config=controller_pdc_pd_all_config.data controller.elf
	${OBJCOPY} --update-section .pdc_channel_all_config=controller_pdc_channel_all_config.data controller.elf


$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

qemu_disk:
	$(SDDF)/tools/mkvirtdisk $@ 1 512 16777216 GPT

qemu: ${IMAGE_FILE} qemu_disk
	$(QEMU) $(QEMU_ARGS)


clean::
	${RM} -rf ${IMAGES}

-include controller.d
