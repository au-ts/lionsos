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
ifeq ($(strip $(LIBMATH)),)
export LIBMATH:=$(dir $(realpath $(shell aarch64-none-elf-gcc --print-file-name libm.a)))
endif

export LIONSOS ?= $(abspath ../..)
export KITTY_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export CONFIG_INCLUDE ?= $(abspath src/config)
export MICROKIT_CONFIG ?= debug
export BUILD_DIR ?= $(abspath build)

ifeq ($(strip $(MICROKIT_BOARD)), odroidc4)
	export NET_DRIV_DIR := meson
	export UART_DRIV_DIR := meson
	export TIMER_DRIV_DIR := meson
	export I2C_DRIV_DIR := meson
	export CPU := cortex-a55
	KITTY_GRAPHICS_VM_ROOTFS := 08c10529dc2806559d5c4b7175686a8206e10494-rootfs.cpio.gz
	KITTY_GRAPHICS_VM_LINUX := 90c4247bcd24cbca1a3db4b7489a835ce87a486e-linux
else ifeq ($(strip $(MICROKIT_BOARD)), qemu_virt_aarch64)
	export NET_DRIV_DIR := virtio
	export UART_DRIV_DIR := arm
	export TIMER_DRIV_DIR := arm
	export CPU := cortex-a53
	export QEMU := qemu-system-aarch64
	KITTY_GRAPHICS_VM_ROOTFS := 8d4a14e2c92a638d68f04832580a57b94e8a4f6b-rootfs.cpio.gz
	KITTY_GRAPHICS_VM_LINUX := 75757e2972d5dc528d98cab377e2c74a9e02d6f9-linux
else
$(error Unsupported MICROKIT_BOARD given)
endif

IMAGE_FILE := $(BUILD_DIR)/kitty.img
REPORT_FILE := $(BUILD_DIR)/report.txt

all: ${IMAGE_FILE}

images:
	mkdir -p ${BUILD_DIR}
	cd ${BUILD_DIR} && curl -L https://lionsos.org/downloads/examples/kitty/$(KITTY_GRAPHICS_VM_ROOTFS) -o rootfs.cpio.gz
	cd ${BUILD_DIR} && curl -L https://lionsos.org/downloads/examples/kitty/$(KITTY_GRAPHICS_VM_LINUX) -o linux

qemu ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: kitty.mk
	mkdir -p ${BUILD_DIR}
	cp kitty.mk $@


submodules:
	git submodule update --init $(LIONSOS)/dep/libvmm
	git submodule update --init $(LIONSOS)/dep/libnfs
	git submodule update --init $(LIONSOS)/dep/micropython
	git submodule update --init $(LIONSOS)/dep/musllibc
	git submodule update --init $(LIONSOS)/dep/sddf
	git submodule update --init $(LIONSOS)/dep/libmicrokitco
	cd ${LIONSOS}/dep/micropython && git submodule update --init lib/micropython-lib

FORCE:
