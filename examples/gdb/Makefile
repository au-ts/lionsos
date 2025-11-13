#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

BUILD_DIR ?= build
export MICROKIT_CONFIG ?= debug

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif

ifeq ($(strip $(MICROKIT_BOARD)),)
$(error MICROKIT_BOARD must be specified)
endif

export MICROKIT_CONFIG ?= debug
export BUILD_DIR:=$(abspath ${BUILD_DIR})
export MICROKIT_SDK:=$(abspath ${MICROKIT_SDK})
export TOP := $(abspath .)
export LIONSOS := $(abspath ../..)

IMAGE_FILE := $(BUILD_DIR)/gdb.img
REPORT_FILE := $(BUILD_DIR)/report.txt

all: ${IMAGE_FILE}

clean clobber qemu ${IMAGE_FILE}: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: gdb_example.mk Makefile
	mkdir -p ${BUILD_DIR}
	echo "export MICROKIT_CONFIG ?= ${MICROKIT_CONFIG}" > $@
	echo "export MICROKIT_SDK ?= ${MICROKIT_SDK}" >> $@
	echo "export BUILD_DIR := ${BUILD_DIR}" >> $@
	echo "export MICROKIT_BOARD ?= ${MICROKIT_BOARD}" >> $@
	echo "export TOP := ${TOP}" >> $@
	echo "export LIONSOS := ${LIONSOS}" >> $@
	cat gdb_example.mk >> $@

FORCE:
