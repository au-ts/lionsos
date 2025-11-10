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

# LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco

IMAGES := timer_driver.elf scheduler.elf \
	$(PART_ELFS)

CFLAGS += \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(ARINC_DIR)/include

LDFLAGS := -L$(BOARD_DIR)/lib -L$(SDDF)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a


all: cache.o
CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@

%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

SDDF_CUSTOM_LIBC := 1

SDDF_MAKEFILES := ${SDDF}/util/util.mk \
	${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk \
	${SDDF}/drivers/serial/${UART_DRIV_DIR}/serial_driver.mk \
	${SDDF}/serial/components/serial_components.mk \

include ${SDDF_MAKEFILES}

vpath %.c $(SDDF) $(ARINC_DIR)/src $(PART_SRC_VPATH)

# LIBMICROKITCO_LIBC_INCLUDE := $(LIONS_LIBC)/include
# include $(LIBMICROKITCO_PATH)/libmicrokitco.mk

${IMAGES}: libsddf_util_debug.a

%.o: %.c ${SDDF}/include
	${CC} ${CFLAGS} -c -o $@ $<

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE)
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_scheduler.data scheduler.elf

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

FORCE:

# ${SDDF_MAKEFILES} ${LIONSOS}/dep/sddf/include &:
# 	cd ${LIONSOS}; git submodule update --init dep/sddf

qemu: $(IMAGE_FILE)
	$(QEMU) -nographic $(QEMU_ARCH_ARGS)

clean::
	${RM} -f *.elf .depend* $
	find . -name \*.[do] |xargs --no-run-if-empty rm

clobber:: clean
	rm -f *.a
	rm -f ${IMAGE_FILE} ${REPORT_FILE}
