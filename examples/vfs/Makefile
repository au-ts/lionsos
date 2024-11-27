#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
override MICROKIT_SDK:=$(abspath ${MICROKIT_SDK})
ifeq ($(strip $(LIBGCC)),)
export LIBGCC:=$(dir $(realpath $(shell aarch64-none-elf-gcc --print-file-name libgcc.a)))
endif
ifeq ($(strip $(LIBMATH)),)
export LIBMATH:=$(dir $(realpath $(shell aarch64-none-elf-gcc --print-file-name libm.a)))
endif

export LIONSOS ?= $(abspath ../..)
export EXAMPLE_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export CONFIG_INCLUDE ?= $(abspath src/config)
export MICROKIT_CONFIG ?= debug
export BUILD_DIR ?= $(abspath build)
export MICROKIT_BOARD ?= qemu_virt_aarch64

IMAGE_FILE := $(BUILD_DIR)/vfs.img
REPORT_FILE := $(BUILD_DIR)/report.txt

all: ${IMAGE_FILE}

qemu ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: vfs.mk
	mkdir -p ${BUILD_DIR}
	cp vfs.mk $@

submodules:
	git submodule update --init $(LIONSOS)/dep/libvmm
	git submodule update --init $(LIONSOS)/dep/libnfs
	git submodule update --init $(LIONSOS)/dep/micropython
	git submodule update --init $(LIONSOS)/dep/musllibc
	git submodule update --init $(LIONSOS)/dep/sddf
	git submodule update --init $(LIONSOS)/dep/libmicrokitco
	cd ${LIONSOS}/dep/micropython && git submodule update --init lib/micropython-lib

FORCE:
