#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif

ifeq ($(DEBUGGER_BACKEND),net)
    UART_DRIV_DIR := arm
else ifeq ($(DEBUGGER_BACKEND),serial)
    UART_DRIV_DIR := virtio
else
$(error Specifiy "DEBUGGER_BACKEND" to either be "net" or "serial")
endif
override MICROKIT_SDK:=$(abspath ${MICROKIT_SDK})

export LIONSOS ?= $(abspath ../..)
export GDB_COMPONENT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export MICROKIT_CONFIG ?= debug
export BUILD_DIR ?= $(abspath build)
export MICROKIT_BOARD ?= qemu_virt_aarch64

IMAGE_FILE := $(BUILD_DIR)/gdb_component.img
REPORT_FILE := $(BUILD_DIR)/report.txt

all: ${IMAGE_FILE}

qemu qemu-gdb ${IMAGE_FILE} ${REPORT_FILE} clean clobber libunwind.a: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: gdb_component.mk Makefile
	mkdir -p ${BUILD_DIR}
	cp gdb_component.mk $@
	echo "export LIONSOS ?= ${LIONSOS}" > $@
	echo "export GDB_COMPONENT_DIR ?= ${GDB_COMPONENT_DIR}" >> $@
	echo "export BUILD_DIR := ${BUILD_DIR}" >> $@
	echo "export MICROKIT_BOARD ?= ${MICROKIT_BOARD}" >> $@
	echo "export MICROKIT_SDK ?= ${MICROKIT_SDK}" >> $@
	echo "export MICROKIT_CONFIG ?= ${MICROKIT_CONFIG}" >> $@
	cat gdb_component.mk >> $@

FORCE:
