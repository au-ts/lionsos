#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
override MICROKIT_SDK:=$(abspath ${MICROKIT_SDK})

ifeq ($(strip $(NFS_SERVER)),)
$(error "NFS_SERVER must be specified: this is the hostname of the NFS server where the webserver will look for the website's files")
endif

ifeq ($(strip $(NFS_DIRECTORY)),)
$(error "NFS_DIRECTORY must be specified: this is the path of the NFS export on the NFS server where the webserver will look for the website's files")
endif

ifeq ($(strip $(WEBSITE_DIR)),)
$(error "WEBSITE_DIR must be specified: this is the filesystem path of the directory containing the website's files, relative to the NFS export")
endif

ifeq ($(strip $(LIBGCC)),)
export LIBGCC:=$(dir $(realpath $(shell aarch64-none-elf-gcc --print-file-name libgcc.a)))
endif
ifeq ($(strip $(LIBMATH)),)
export LIBMATH:=$(dir $(realpath $(shell aarch64-none-elf-gcc --print-file-name libm.a)))
endif


export MICROKIT_CONFIG ?= debug
export BUILD_DIR ?= $(abspath build)
export MICROKIT_BOARD := $(MICROKIT_BOARD)
export CONFIG_INCLUDE ?= $(abspath config)
export WEBSERVER_SRC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export LIONSOS := $(abspath ../..)

IMAGE_FILE := $(BUILD_DIR)/webserver.img
REPORT_FILE := $(BUILD_DIR)/report.txt
nproc:= $(shell sh -c "if test -f /proc/cpuinfo; then grep processor /proc/cpuinfo | wc -l; else echo 2; fi")

all: ${IMAGE_FILE}

qemu ${IMAGE_FILE}: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: webserver.mk
	mkdir -p ${BUILD_DIR}
	cp webserver.mk $@

FORCE:
