#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
override MICROKIT_SDK:=$(abspath ${MICROKIT_SDK})

export LIONSOS ?= $(abspath ../..)
export POSIX_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export MICROKIT_CONFIG ?= debug
export BUILD_DIR ?= $(abspath build)
export MICROKIT_BOARD ?= qemu_virt_aarch64

IMAGE_FILE := $(BUILD_DIR)/posix.img
REPORT_FILE := $(BUILD_DIR)/report.txt

all: ${IMAGE_FILE}

qemu ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: posix.mk
	mkdir -p ${BUILD_DIR}
	cp posix.mk $@

submodules:
	git submodule update --init $(LIONSOS)/dep/libnfs
	git submodule update --init $(LIONSOS)/dep/micropython
	git submodule update --init $(LIONSOS)/dep/sddf
	git submodule update --init $(LIONSOS)/dep/libmicrokitco
	cd ${LIONSOS}/dep/micropython && git submodule update --init lib/micropython-lib

FORCE:
