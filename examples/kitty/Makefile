#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
override MICROKIT_SDK:=$(abspath ${MICROKIT_SDK})
ifeq ($(strip ${NFS_SERVER}),)
$(error NFS_SERVER needs to be an IP address)
endif
ifeq ($(strip ${NFS_DIRECTORY}),)
$(error NFS_DIRECTORY needs to be the name of an exported directory)
endif

export LIONSOS ?= $(abspath ../..)
export KITTY_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export MICROKIT_CONFIG ?= debug
export BUILD_DIR ?= $(abspath build)
export MICROKIT_BOARD := odroidc4
export CPU := cortex-a55

IMAGE_FILE := $(BUILD_DIR)/kitty.img
REPORT_FILE := $(BUILD_DIR)/report.txt

all: ${IMAGE_FILE}

qemu ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: kitty.mk
	mkdir -p ${BUILD_DIR}
	cp kitty.mk $@


submodules:
	git submodule update --init $(LIONSOS)/dep/libvmm
	git submodule update --init $(LIONSOS)/dep/libnfs
	git submodule update --init $(LIONSOS)/dep/micropython
	git submodule update --init $(LIONSOS)/dep/musllibc
	git submodule update --init $(LIONSOS)/dep/sddf
	git submodule update --init $(LIONSOS)/dep/libmicrokitco
	cd ${LIONSOS}/dep/micropython && git submodule update --init lib/micropython-lib

FORCE:
