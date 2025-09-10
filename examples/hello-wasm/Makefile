ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
override MICROKIT_SDK:=$(abspath ${MICROKIT_SDK})

export MICROKIT_CONFIG ?= debug
export BUILD_DIR ?= $(abspath build)
export MICROKIT_BOARD ?= qemu_virt_aarch64
export HELLO_WASM_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export LIONSOS ?= $(abspath ../..)

IMAGE_FILE := $(BUILD_DIR)/hello-wasm.img
REPORT_FILE := $(BUILD_DIR)/report.txt
nproc:= $(shell sh -c "if test -f /proc/cpuinfo; then grep processor /proc/cpuinfo | wc -l; else echo 2; fi")

all: ${IMAGE_FILE}

qemu ${IMAGE_FILE}: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -j${nproc} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: hello-wasm.mk
	mkdir -p ${BUILD_DIR}
	cp hello-wasm.mk $@


FORCE:
