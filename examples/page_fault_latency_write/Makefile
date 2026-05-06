#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif

ifeq ($(strip $(MICROKIT_BOARD)),)
$(error MICROKIT_BOARD must be specified)
endif
BUILD_DIR ?= build
override BUILD_DIR := $(abspath ${BUILD_DIR})
export BUILD_DIR
export SDDF := $(abspath ../../dep/sddf)
override MICROKIT_SDK := $(abspath ${MICROKIT_SDK})
BENCHMARK := $(SDDF)/benchmark
SERIAL_COMPONENTS := $(SDDF)/serial/components
UART_DRIVER := $(SDDF)/drivers/serial/$(UART_DRIV_DIR)

IMAGE_FILE := ${BUILD_DIR}/loader.img # maybe this line needs to be changed...
# REPORT_FILE := ${BUILD_DIR}/report.txt

all: ${IMAGE_FILE} 

qemu ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR}  MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)
maaxboard ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR}  MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)
rpi4b ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR}  MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

mkdir -p ${BUILD_DIR}


#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This Makefile is copied into the build directory
# and operated on from there.
#

SUPPORTED_BOARDS := \
	qemu_virt_aarch64 \
	maaxboard \
	rpi4b



ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif

ifeq ($(strip $(SDDF)),)
$(error SDDF must be specified)
endif

ifeq ($(strip $(LIONSOS)),)
$(error LIONSOS must be specified)
endif

ifeq ($(strip $(TOOLCHAIN)),)
	TOOLCHAIN := clang
endif

BUILD_DIR ?= build
MICROKIT_CONFIG ?= debug

# Allow to user to specify a custom partition
PARTITION :=
ifdef PARTITION
	PARTITION_ARG := --partition $(PARTITION)
endif

IMAGE_FILE := loader.img
REPORT_FILE  := report.txt

SYSTEM_FILE := pfl.system



TOP := ${LIONSOS}/examples/pfl
CONFIGS_INCLUDE := ${TOP}
SDDF_CUSTOM_LIBC := 1
SPEC = $(BUILD_DIR)/capdl_spec.json




IMAGES := pager.elf client.elf timer_driver.elf benchmark.elf idle.elf serial_driver.elf serial_virt_tx.elf
CFLAGS +=  -O3 -Wall -Wno-unused-function -Wno-unused-command-line-argument \
		  -I$(SDDF)/include \
		  -I$(SDDF)/include/microkit \
		  -I$(CONFIGS_INCLUDE)

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := --start-group -lmicrokit -Tmicrokit.ld libsddf_util_debug.a --end-group

METAPROGRAM := $(TOP)/meta.py

# SERIAL_DRIVER := $(SDDF)/drivers/serial/${UART_DRIV_DIR}

all: $(IMAGE_FILE)

include ${SDDF}/tools/make/board/common.mk
# include ${SDDF}/drivers/serial/${UART_DRIV_DIR}/serial_driver.mk

# ifdef BLK_NEED_TIMER
# include ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk
# IMAGES += timer_driver.elf
# export BLK_NEED_TIMER
# endif

include ${SDDF}/util/util.mk
include ${SDDF}/serial/components/serial_components.mk
include ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk
include ${BENCHMARK}/benchmark.mk

${IMAGES}: libsddf_util_debug.a

# TODO: i need to update below to be up to date.

pager.o: ${TOP}/pager.c 
	$(CC) -c $(CFLAGS) -I. $< -o pager.o
pager.elf: pager.o libsddf_util_debug.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

client.o: ${TOP}/client.c
	$(CC) -c $(CFLAGS) -I. $< -o client.o
client.elf: client.o libsddf_util_debug.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@


$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	PYTHONPATH=${SDDF}/tools/meta:$$PYTHONPATH $(PYTHON) \
		$(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB)\
		--output . --sdf $(SYSTEM_FILE) $(PARTITION_ARG) 
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	touch $@

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)  --capdl-json $(SPEC)

maaxboard_disk: $(SDDF)/tools/mkvirtdisk disk 1 512 16777216 GPT

maaxboard: ${IMAGE_FILE} maaxboard_disk
	$(QEMU) $(QEMU_ARCH_ARGS) $(QEMU_BLK_ARGS) \
	    -nographic \
	    -d guest_errors \
	    -drive file=disk,if=none,format=raw,id=hd

qemu_disk:
	$(SDDF)/tools/mkvirtdisk disk 1 512 16777216 GPT

qemu: ${IMAGE_FILE} qemu_disk
	$(QEMU) $(QEMU_ARCH_ARGS) $(QEMU_BLK_ARGS) \
	    -nographic \
	    -d guest_errors \
	    -drive file=disk,if=none,format=raw,id=hd

rpi4b_disk: $(SDDF)/tools/mkvirtdisk disk 1 512 16777216 GPT
rpi4b: ${IMAGE_FILE} qemu_disk
	$(QEMU) $(QEMU_ARCH_ARGS) $(QEMU_BLK_ARGS) \
	    -nographic \
	    -d guest_errors \
	    -drive file=disk,if=none,format=raw,id=hd

# clean::
# 	rm -f client.o
# clobber:: clean
# 	rm -f client.elf ${IMAGE_FILE} ${REPORT_FILE}
