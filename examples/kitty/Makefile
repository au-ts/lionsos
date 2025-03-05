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
ifeq ($(strip $(LIBGCC)),)
export LIBGCC:=$(dir $(realpath $(shell aarch64-none-elf-gcc --print-file-name libgcc.a)))
endif

export LIONSOS ?= $(abspath ../..)
export KITTY_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export MICROKIT_CONFIG ?= debug
export BUILD_DIR ?= $(abspath build)

export KITTY_GRAPHICS_VM_ROOTFS := 08c10529dc2806559d5c4b7175686a8206e10494-rootfs.cpio.gz
export KITTY_GRAPHICS_VM_LINUX := 90c4247bcd24cbca1a3db4b7489a835ce87a486e-linux

IMAGE_FILE := $(BUILD_DIR)/kitty.img
REPORT_FILE := $(BUILD_DIR)/report.txt

all: ${IMAGE_FILE}

qemu ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: kitty.mk
	mkdir -p ${BUILD_DIR}
	cp kitty.mk $@

FORCE:
