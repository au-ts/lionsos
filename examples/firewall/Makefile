#
# Copyright 2025, UNSW
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
export FIREWALL_SRC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export LIONSOS := $(abspath ../..)

IMAGE_FILE := $(BUILD_DIR)/firewall.img
REPORT_FILE := $(BUILD_DIR)/report.txt
nproc:= $(shell sh -c "if test -f /proc/cpuinfo; then grep processor /proc/cpuinfo | wc -l; else echo 2; fi")

all: ${IMAGE_FILE}

clean clobber qemu ${IMAGE_FILE}: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: firewall.mk Makefile
	mkdir -p ${BUILD_DIR}
	echo "export MICROKIT_CONFIG ?= ${MICROKIT_CONFIG}" > $@
	echo "export MICROKIT_SDK ?= ${MICROKIT_SDK}" >> $@
	echo "export BUILD_DIR := ${BUILD_DIR}" >> $@
	echo "export MICROKIT_BOARD ?= ${MICROKIT_BOARD}" >> $@
	echo "export FIREWALL_SRC_DIR := ${FIREWALL_SRC_DIR}" >> $@
	echo "export LIONSOS := ${LIONSOS}" >> $@
	cat firewall.mk >> $@

FORCE:
