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
export RR_COMPONENT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export MICROKIT_CONFIG ?= debug
export BUILD_DIR ?= $(abspath build)
export MICROKIT_BOARD ?= qemu_virt_aarch64

IMAGE_FILE := $(BUILD_DIR)/rr_component.img
REPORT_FILE := $(BUILD_DIR)/report.txt

all: ${IMAGE_FILE}

qemu qemu-gdb ${IMAGE_FILE} ${REPORT_FILE} clean clobber libunwind.a: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: rr_component.mk Makefile
	mkdir -p ${BUILD_DIR}
	cp rr_component.mk $@
	echo "export LIONSOS ?= ${LIONSOS}" > $@
	echo "export RR_COMPONENT_DIR ?= ${RR_COMPONENT_DIR}" >> $@
	echo "export BUILD_DIR := ${BUILD_DIR}" >> $@
	echo "export MICROKIT_BOARD ?= ${MICROKIT_BOARD}" >> $@
	echo "export MICROKIT_SDK ?= ${MICROKIT_SDK}" >> $@
	echo "export MICROKIT_CONFIG ?= ${MICROKIT_CONFIG}" >> $@
	cat rr_component.mk >> $@

FORCE:
