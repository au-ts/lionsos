#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif

ifeq ($(strip $(MICROKIT_BOARD)),)
$(error MICROKIT_BOARD must be specified)
endif
ifeq ($(strip $(LIONSOS)),)
$(error LIONSOS must be specified)
endif
BUILD_DIR ?= build
override BUILD_DIR := $(abspath ${BUILD_DIR})
export BUILD_DIR
# This example's own directory: where src/, meta.py and the .mk live. The build
# runs with ${BUILD_DIR} as its cwd, so the .mk reaches its sources through this.
export TOP := $(abspath $(dir $(lastword ${MAKEFILE_LIST})))
export SDDF := $(abspath ${LIONSOS}/dep/sddf)
override MICROKIT_SDK := $(abspath ${MICROKIT_SDK})
export MICROKIT_CONFIG ?= benchmark
SMP_CONFIG ?=core_config/single_core.json
IMAGE_FILE := ${BUILD_DIR}/loader.img

all: ${IMAGE_FILE}

qemu ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} BUILD_DIR=${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} SMP_CONFIG=${SMP_CONFIG} $(notdir $@)


${BUILD_DIR}/Makefile: 519.mk
	mkdir -p ${BUILD_DIR}
	cp 519.mk $@

submodules:
	git submodule update --init $(LIONSOS)/dep/musllibc
	git submodule update --init $(LIONSOS)/dep/sddf
	git submodule update --init $(LIONSOS)/dep/benchmarking

FORCE:
