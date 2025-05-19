#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
override MICROKIT_SDK := $(abspath ${MICROKIT_SDK})
export EXAMPLE_DIR := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
LIONSOS ?= ../../
export LIONSOS := $(abspath ${LIONSOS})
export SDDF := ${LIONSOS}/dep/sddf
export MICROKIT_CONFIG ?= debug
BUILD_DIR ?= build
export BUILD_DIR := $(abspath ${BUILD_DIR})
export MICROKIT_BOARD := odroidc4
export VMM_IMAGE_DIR := ${EXAMPLE_DIR}/vmm
export ORIGINAL_DTB := $(VMM_IMAGE_DIR)/meson-sm1-odroid-c4.dtb

VARIANT ?= passthrough
ifeq (${VARIANT},passthrough)
export SYSTEM := ${EXAMPLE_DIR}/vmm.system
export DT_OVERLAYS := ${VMM_IMAGE_DIR}/overlay.dts
VAR_MAKEFILE := vmm-dev.mk
else
export SYSTEM := ${EXAMPLE_DIR}/vmm-virtio-console.system
export DT_OVERLAYS := ${VMM_IMAGE_DIR}/overlay-virtcon.dts
VAR_MAKEFILE := vmm-virtio-console.mk
endif

IMAGE_FILE := $(BUILD_DIR)/vmdev.img
REPORT_FILE := $(BUILD_DIR)/report.txt

all: $(IMAGE_FILE)

${REPORT_FILE} ${IMAGE_FILE}: ${BUILD_DIR}/Makefile ${SYSTEM_FILE} ${OVERLAY} FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK}


${BUILD_DIR}/Makefile: ${VAR_MAKEFILE} FORCE
	mkdir -p ${BUILD_DIR}
	[ cmp $@ ${VAR_MAKEFILE} > /dev/null 2>&1 ] || cp $< $@

clean:
	${MAKE} -C ${BUILD_DIR} $@
clobber:
	rm -rf ${BUILD_DIR}

FORCE: ;

