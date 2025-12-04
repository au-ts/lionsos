#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

TOOLCHAIN := clang
CC := clang
LD := ld.lld
RANLIB := llvm-ranlib
AR := llvm-ar
OBJCOPY := llvm-objcopy
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
PYTHON ?= python3
DTC := dtc
SDDF := $(LIONSOS)/dep/sddf

SYSTEM_FILE := arinc_scheduling.system
IMAGE_FILE := arinc_scheduling.img
REPORT_FILE := report.txt

SUPPORTED_BOARDS := qemu_virt_aarch64

include ${SDDF}/tools/make/board/common.mk

METAPROGRAM := $(ARINC_DIR)/meta.py

SDFGEN_HELPER := $(ARINC_DIR)/sdfgen_helper.py
# Macros needed by sdfgen helper to calculate config struct sizes
SDFGEN_UNKOWN_MACROS := MAX_PARTITIONS=61
# Headers containing config structs and dependencies
SCHEDULER_CONFIG_HEADERS := $(ARINC_DIR)/include/scheduler_config.h

IMAGES := timer_driver.elf scheduler.elf \
	$(PART_ELFS)

CFLAGS += \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(ARINC_DIR)/include \
	-I$(ARINC_DIR)/include/types

LDFLAGS := -L$(BOARD_DIR)/lib -L$(SDDF)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a


all: cache.o
CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@


SDDF_CUSTOM_LIBC := 1

SDDF_MAKEFILES := ${SDDF}/util/util.mk \
	${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk \
	${SDDF}/drivers/serial/${UART_DRIV_DIR}/serial_driver.mk \
	${SDDF}/serial/components/serial_components.mk \

include ${SDDF_MAKEFILES}

vpath %.c $(SDDF) $(ARINC_DIR)/src $(ARINC_DIR)/src/queue $(PART_SRC_VPATH)

${IMAGES}: libsddf_util_debug.a

p1_upd.elf: $(PART_1_UPD_OBJS)
	${LD} ${LDFLAGS} -o $@ $(PART_1_UPD_OBJS) ${LIBS}

p2_upd.elf: $(PART_2_UPD_OBJS)
	${LD} ${LDFLAGS} -o $@ $(PART_2_UPD_OBJS) ${LIBS}

p3_upd.elf: $(PART_3_UPD_OBJS)
	${LD} ${LDFLAGS} -o $@ $(PART_3_UPD_OBJS) ${LIBS}

%.o: %.c ${SDDF}/include
	${CC} ${CFLAGS} -c -o $@ $<

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	$(PYTHON) $(SDFGEN_HELPER) --macros "$(SDFGEN_UNKOWN_MACROS)" --configs "$(SCHEDULER_CONFIG_HEADERS)" --output $(BUILD_DIR)/config_structs.py
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE) --objcopy $(OBJCOPY)
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_scheduler.data scheduler.elf

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

FORCE:

qemu: $(IMAGE_FILE)
	$(QEMU) -nographic $(QEMU_ARCH_ARGS)

clean::
	${RM} -f *.elf .depend* $
	find . -name \*.[do] |xargs --no-run-if-empty rm

clobber:: clean
	rm -f *.a
	rm -f ${IMAGE_FILE} ${REPORT_FILE}
