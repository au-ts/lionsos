#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
override MICROKIT_SDK:=$(abspath ${MICROKIT_SDK})

export MICROKIT_CONFIG ?= debug
export BUILD_DIR ?= $(abspath build)
export MICROKIT_BOARD := $(MICROKIT_BOARD)
export STACK_TRACE_SRC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export LIONSOS := $(abspath ../..)

IMAGE_FILE := $(BUILD_DIR)/stack_trace.img
REPORT_FILE := $(BUILD_DIR)/report.txt
nproc:= $(shell sh -c "if test -f /proc/cpuinfo; then grep processor /proc/cpuinfo | wc -l; else echo 2; fi")

all: ${IMAGE_FILE}

qemu ${IMAGE_FILE}: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

# we now build the libunwind inside te
${BUILD_DIR}/Makefile: stack_trace.mk
	mkdir -p ${BUILD_DIR}
	echo "MICROKIT_SDK ?= ${MICROKIT_SDK}" > $@
	echo "MICROKIT_BOARD ?= ${MICROKIT_BOARD}" >> $@
	echo "MICROKIT_CONFIG ?= ${MICROKIT_CONFIG}" >> $@
	echo "BUILD_DIR := ${BUILD_DIR}" >> $@
	echo "STACK_TRACE_SRC_DIR ?= ${STACK_TRACE_SRC_DIR}" >> $@
	echo "LIONSOS := ${LIONSOS}" >> $@
	cat stack_trace.mk >> $@

FORCE:
