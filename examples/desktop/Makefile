#
# Copyright 2026, LionsOS Contributors
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
override MICROKIT_SDK := $(abspath ${MICROKIT_SDK})

export LIONSOS ?= $(abspath ../..)
export DESKTOP_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export MICROKIT_BOARD ?= qemu_virt_aarch64
export MICROKIT_CONFIG ?= debug
export BUILD_DIR ?= $(abspath build)

IMAGE_FILE := $(BUILD_DIR)/desktop.img
REPORT_FILE := $(BUILD_DIR)/report.txt

# `all` also builds the WebAssembly apps that `qemu` puts on the disk
all qemu ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: desktop.mk
	mkdir -p ${BUILD_DIR}
	echo "export LIONSOS := ${LIONSOS}" > $@
	echo "export DESKTOP_DIR := ${DESKTOP_DIR}" >> $@
	echo "export MICROKIT_SDK ?= ${MICROKIT_SDK}" >> $@
	echo "export MICROKIT_CONFIG ?= ${MICROKIT_CONFIG}" >> $@
	echo "export MICROKIT_BOARD ?= ${MICROKIT_BOARD}" >> $@
	echo "export BUILD_DIR := ${BUILD_DIR}" >> $@
	echo "include ${DESKTOP_DIR}/desktop.mk" >> $@

FORCE:
