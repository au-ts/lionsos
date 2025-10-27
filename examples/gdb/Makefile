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

ifeq ($(strip $(TOOLCHAIN)),)
	TOOLCHAIN := clang
endif

ifeq ($(strip $(TOOLCHAIN)), clang)
	export CC := clang
	export LD := ld.lld
	export AR := llvm-ar
	export RANLIB := llvm-ranlib
	export OBJCOPY := llvm-objcopy
else
	export CC := $(TOOLCHAIN)-gcc
	export LD := $(TOOLCHAIN)-ld
	export AS := $(TOOLCHAIN)-as
	export AR := $(TOOLCHAIN)-ar
	export RANLIB := $(TOOLCHAIN)-ranlib
	export OBJCOPY := $(TOOLCHAIN)-objcopy
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
	export ARCH := aarch64
	QEMU := qemu-system-aarch64
else
$(error Unsupported MICROKIT_BOARD given)
endif

ifeq ($(ARCH),aarch64)
	export CFLAGS_ARCH := -mcpu=$(CPU)
	export TARGET := aarch64-none-elf
else ifeq ($(ARCH),riscv64)
	export CFLAGS_ARCH := -march=rv64imafdc
	export TARGET := riscv64-none-elf
else
$(error Unsupported ARCH given)
endif

ifeq ($(strip $(TOOLCHAIN)), clang)
	CFLAGS_ARCH += -target $(TARGET)
endif

export BUILD_DIR:=$(abspath ${BUILD_DIR})
export MICROKIT_SDK:=$(abspath ${MICROKIT_SDK})
export TOP := $(abspath .)

export MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit

export LIONSOS := $(abspath ../..)
export SDDF=$(LIONSOS)/dep/sddf
export LIBGDB_DIR=$(LIONSOS)/dep/libgdb
export LIBVSPACE_DIR=$(LIBGDB_DIR)/libvspace

export DEBUGGER_INCLUDE:=$(TOP)/net_debugger/include

IMAGE_FILE := $(BUILD_DIR)/loader.img
REPORT_FILE := $(BUILD_DIR)/report.txt

all: ${IMAGE_FILE}

qemu ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE}  -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: gdb_example.mk
	mkdir -p ${BUILD_DIR}
	cp gdb_example.mk $@

FORCE:
