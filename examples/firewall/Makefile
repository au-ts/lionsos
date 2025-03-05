#
# Copyright 2022, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

BUILD_DIR ?= build
export MICROKIT_CONFIG ?= debug

ifeq ($(strip $(LIBMATH)),)
export LIBMATH:=$(dir $(realpath $(shell aarch64-none-elf-gcc --print-file-name libm.a)))
endif

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
export override MICROKIT_SDK:=$(abspath ${MICROKIT_SDK})

ifeq ($(strip $(TOOLCHAIN)),)
	TOOLCHAIN := aarch64-none-elf
	export TOOLCHAIN := aarch64-none-elf
	export LIBC := $(dir $(realpath $(shell aarch64-none-elf-gcc --print-file-name libc.a)))
endif

ifeq ($(strip $(MICROKIT_BOARD)), imx8mp_evk)
	export DRIV_DIR := imx
	export UART_DRIV_DIR := imx
	export TIMER_DRV_DIR := imx
	export CPU := cortex-a53
else ifeq ($(strip $(MICROKIT_BOARD)), qemu_virt_aarch64)
	export DRIV_DIR := virtio
	export UART_DRIV_DIR := arm
	export TIMER_DRV_DIR := arm
	export CPU := cortex-a53
	QEMU := qemu-system-aarch64
else
$(error Unsupported MICROKIT_BOARD given)
endif

export BUILD_DIR:=$(abspath ${BUILD_DIR})
export MICROKIT_SDK:=$(abspath ${MICROKIT_SDK})
export TOP := $(abspath $(dir ${MAKEFILE_LIST}))

export CC := $(TOOLCHAIN)-gcc
export LD := $(TOOLCHAIN)-ld
export AS := $(TOOLCHAIN)-as
export AR := $(TOOLCHAIN)-ar
export RANLIB := $(TOOLCHAIN)-ranlib
export OBJCOPY := llvm-objcopy
export MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
export LIONSOS ?= $(abspath ../..)
export FIREWALL_INCLUDE:=$(abspath .)/include
export CONFIG_INCLUDE ?= $(abspath src/config)

IMAGE_FILE := $(BUILD_DIR)/loader.img
REPORT_FILE := $(BUILD_DIR)/report.txt

all: ${IMAGE_FILE}

qemu ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE}  -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: firewall.mk
	mkdir -p ${BUILD_DIR}
	cp firewall.mk $@

FORCE:
