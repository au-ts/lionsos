#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

export LIONSOS ?= $(abspath ../..)
export MICROKIT_SDK := $(LIONSOS)/microkit-sdk-2.2.0

# Fixed typo: changed VULKAN_DIRECTORY to VULKAN_DIR to match the echo statements below
export VULKAN_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export MICROKIT_CONFIG ?= debug
export BUILD_DIR ?= $(abspath build)

# Graphics VM images
export VULKAN_GRAPHICS_VM_ROOTFS := 8d4a14e2c92a638d68f04832580a57b94e8a4f6b-rootfs.cpio.gz
export VULKAN_GRAPHICS_VM_LINUX := 75757e2972d5dc528d98cab377e2c74a9e02d6f9-linux

IMAGE_FILE := $(BUILD_DIR)/vulkan.img
REPORT_FILE := $(BUILD_DIR)/report.txt

all: ${IMAGE_FILE}

qemu ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: vulkan.mk
	@mkdir -p ${BUILD_DIR}
	@echo "export LIONSOS := ${LIONSOS}" > $@
	@echo "export VULKAN_DIR := ${VULKAN_DIR}" >> $@
	@echo "export MICROKIT_SDK ?= ${MICROKIT_SDK}" >> $@
	@echo "export MICROKIT_CONFIG ?= ${MICROKIT_CONFIG}" >> $@
	@echo "export MICROKIT_BOARD ?= ${MICROKIT_BOARD}" >> $@
	@echo "export BUILD_DIR := ${BUILD_DIR}" >> $@
	@echo "export VULKAN_GRAPHICS_VM_ROOTFS ?= ${VULKAN_GRAPHICS_VM_ROOTFS}" >> $@
	@echo "export VULKAN_GRAPHICS_VM_LINUX ?= ${VULKAN_GRAPHICS_VM_LINUX}" >> $@
	@cat vulkan.mk >> $@

FORCE: