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
export MICROKIT_CONFIG ?= debug
SMP_CONFIG ?=core_config/single_core.json
IMAGE_FILE := ${BUILD_DIR}/loader.img 

all: ${IMAGE_FILE} 

qemu ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR}  MICROKIT_SDK=${MICROKIT_SDK} SMP_CONFIG=${SMP_CONFIG} $(notdir $@)
maaxboard ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR}  MICROKIT_SDK=${MICROKIT_SDK} SMP_CONFIG=${SMP_CONFIG} $(notdir $@)


${BUILD_DIR}/Makefile: pager.mk
	mkdir -p ${BUILD_DIR}
	cp pager.mk $@

submodules:
	git submodule update --init $(LIONSOS)/dep/musllibc
	git submodule update --init $(LIONSOS)/dep/sddf
	git submodule update --init $(LIONSOS)/dep/libmicrokitco

FORCE: