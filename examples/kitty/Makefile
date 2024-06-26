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
nproc:= $(shell sh -c "if test -f /proc/cpuinfo; then grep processor /proc/cpuinfo | wc -l; else echo 2; fi")

all: ${IMAGE_FILE}


qemu ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -j${nproc} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

MAKEFILE_CSUM := .makefile_config-$(shell echo -- ${NFS_SERVER} ${NFS_DIRECTORY} ${MICROKIT_SDK} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')
${BUILD_DIR}/${MAKEFLAG_CSUM}:
	mkdir -p ${BUILD_DIR}
	-rm -f ${BUILD}/.makefile_config-*
	> $@

${BUILD_DIR}/Makefile: kitty.mk ${BUILD_DIR}/${MAKEFLAG_CSUM}
	mkdir -p ${BUILD_DIR}
	echo 'MICROKIT_SDK:=${MICROKIT_SDK}' > $@
	echo 'MICROKIT_CONFIG:=${MICROKIT_CONFIG}' >> $@
	echo 'MICROKIT_BOARD:=${MICROKIT_BOARD}' >> $@
	echo 'NFS_SERVER:=${NFS_SERVER}' >> $@
	echo 'NFS_DIRECTORY:=${NFS_DIRECTORY}' >> $@
	echo 'LIONS_OS:=${LIONS_OS}' >> $@
	echo 'KITTY_DIR:=${KITTY_DIR}' >> $@
	cp kitty.mk $@

submodules:
	git submodule update --init $(LIONSOS)/dep/libvmm
	git submodule update --init $(LIONSOS)/dep/libnfs
	git submodule update --init $(LIONSOS)/dep/micropython
	git submodule update --init $(LIONSOS)/dep/musllibc
	git submodule update --init $(LIONSOS)/dep/sddf
	cd ${LIONSOS}/dep/micropython && git submodule update --init lib/micropython-lib


FORCE:
