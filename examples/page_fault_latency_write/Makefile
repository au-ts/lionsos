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

SERIAL_COMPONENTS := $(SDDF)/serial/components
UART_DRIVER := $(SDDF)/drivers/serial/$(UART_DRIV_DIR)
# BENCHMARK := $(SDDF)/benchmark
IMAGE_FILE := ${BUILD_DIR}/loader.img # maybe this line needs to be changed...
REPORT_FILE := ${BUILD_DIR}/report.txt

all: ${IMAGE_FILE} 

qemu ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR}  MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}:
	mkdir -p ${BUILD_DIR}


#
${BUILD_DIR}/Makefile: pfl.mk
	mkdir -p ${BUILD_DIR}
	cp pfl.mk $@

FORCE: