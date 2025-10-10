#
# Copyright 2022, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

BUILD_DIR ?= build
export MICROKIT_CONFIG ?= debug

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
export override MICROKIT_SDK:=$(abspath ${MICROKIT_SDK})

ifeq ($(strip $(TOOLCHAIN)),)
	TOOLCHAIN := aarch64-none-elf
	export TOOLCHAIN := aarch64-none-elf
	export LIBC := $(dir $(realpath $(shell aarch64-none-elf-gcc --print-file-name libc.a)))
endif

ifeq ($(strip $(MICROKIT_BOARD)), odroidc4)
	export DRIV_DIR := meson
	export SERIAL_DRIV_DIR := meson
	export TIMER_DRV_DIR := meson
	export CPU := cortex-a55
else ifeq ($(strip $(MICROKIT_BOARD)), odroidc2)
	export DRIV_DIR := meson
	export SERIAL_DRIV_DIR := meson
	export TIMER_DRV_DIR := meson
	export CPU := cortex-a53
else ifneq ($(filter $(strip $(MICROKIT_BOARD)),imx8mm_evk imx8mp_evk imx8mq_evk maaxboard),)
	export DRIV_DIR := imx
	export SERIAL_DRIV_DIR := imx
	export TIMER_DRV_DIR := imx
	export CPU := cortex-a53
else ifeq ($(strip $(MICROKIT_BOARD)), qemu_virt_aarch64)
	export DRIV_DIR := virtio
	export SERIAL_DRIV_DIR := arm
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
export OBJCOPY := $(TOOLCHAIN)-objcopy
export MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit

export LIONSOS := $(abspath ../..)
export SDDF=$(LIONSOS)/dep/sddf
export LIBGDB_DIR=$(LIONSOS)/dep/libgdb
export LIBVSPACE_DIR=$(LIBGDB_DIR)/libvspace

export SDDF=$(abspath ../../sddf)
export LIBGDB_DIR=$(abspath ../../)
export LIBVSPACE_DIR=$(abspath ../../libvspace)
export DEBUGGER_INCLUDE:=$(abspath .)/apps/debugger/include

IMAGE_FILE := $(BUILD_DIR)/loader.img
REPORT_FILE := $(BUILD_DIR)/report.txt

all: ${IMAGE_FILE}

qemu ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE}  -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: sddf_net_example.mk
	mkdir -p ${BUILD_DIR}
	cp sddf_net_example.mk $@

FORCE:
