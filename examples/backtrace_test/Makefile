#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
override MICROKIT_SDK:=$(abspath ${MICROKIT_SDK})

export LIONSOS ?= $(abspath ../..)
export BACKTRACE_TEST_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export MICROKIT_CONFIG ?= debug
export BUILD_DIR ?= $(abspath build)
export MICROKIT_BOARD ?= qemu_virt_aarch64

IMAGE_FILE := $(BUILD_DIR)/backtrace_test.img
REPORT_FILE := $(BUILD_DIR)/report.txt

all: ${IMAGE_FILE}

qemu qemu-gdb ${IMAGE_FILE} ${REPORT_FILE} clean clobber libunwind.a: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: backtrace_test.mk Makefile
	mkdir -p ${BUILD_DIR}
	cp backtrace_test.mk $@
	echo "export LIONSOS ?= ${LIONSOS}" > $@
	echo "export BACKTRACE_TEST_DIR ?= ${BACKTRACE_TEST_DIR}" >> $@
	echo "export BUILD_DIR := ${BUILD_DIR}" >> $@
	echo "export MICROKIT_BOARD ?= ${MICROKIT_BOARD}" >> $@
	echo "export MICROKIT_SDK ?= ${MICROKIT_SDK}" >> $@
	echo "export MICROKIT_CONFIG ?= ${MICROKIT_CONFIG}" >> $@
	cat backtrace_test.mk >> $@

submodules:
	git submodule update --init $(LIONSOS)/dep/sddf
	git submodule update --init $(LIONSOS)/dep/libmicrokitco
	git submodule update --init $(LIONSOS)/dep/libunwind

FORCE:
