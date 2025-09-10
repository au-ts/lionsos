ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
override MICROKIT_SDK:=$(abspath ${MICROKIT_SDK})

export LIONSOS ?= $(abspath ../..)
export HELLO_WASM_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export MICROKIT_CONFIG ?= debug
export BUILD_DIR ?= $(abspath build)
export MICROKIT_BOARD ?= qemu_virt_aarch64

IMAGE_FILE := $(BUILD_DIR)/hello-wasm.img
REPORT_FILE := $(BUILD_DIR)/report.txt

all: ${IMAGE_FILE}

qemu ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: hello-wasm.mk
	mkdir -p ${BUILD_DIR}
	cp hello-wasm.mk $@

submodules:
	git submodule update --init $(LIONSOS)/dep/wasm-micro-runtime
	git submodule update --init $(LIONSOS)/dep/musllibc
	git submodule update --init $(LIONSOS)/dep/sddf

FORCE:
