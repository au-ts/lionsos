#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This Makefile is copied into the build directory
# and used from there.

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif

ifeq ($(strip $(LIBGCC)),)
LIBGCC := $(shell dirname $$(aarch64-none-elf-gcc --print-file-name libgcc.a))
endif

ifeq ($(strip $(LionsOS)),)
$(error LionsOS should point to the root of the LionOS source tree)
endif

ifeq ($(strip $(EXAMPLE_DIR)),)
$(error EXAMPLE_DIR should contain the name of the directory containing the VMM example)
endif

MICROKIT_CONFIG ?= debug
MICROKIT_BOARD := odroidc4
CPU := cortex-a55

TOOLCHAIN := clang
CC := clang
LD := ld.lld
TARGET := aarch64-none-elf
CHECK_VARIANT:=.variant.$(shell echo ${SYSTEM} ${MICROKIT_CONFIG} ${MICROKIT_SDK} | md5sum - | sed 's/ *-$$//')
.variant.%:
	-rm -f .variant.*
	echo "${VARIANT} ${MICROKIT_CONFIG}" > $@

CHECK_MICROKIT_VARS:=.microkit.$(shell echo ${MICROKIT_CONFIG} ${BOARD} |md5sum - | sed 's/ *-$$//')
.microkit.%:
	-rm -f .microkit.%
	echo  ${MICROKIT_CONFIG} ${BOARD} > $@

# Uncomment these lines to use an uninstalled microkit
#export PYTHONPATH := $(realpath $(MICROKIT_SDK)/../../tool/)
#MICROKIT_TOOL =  /usr/bin/python3 ${PYTHONPATH}/microkit/__main__.py

MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
DTC := dtc

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
LIBVMM_DIR ?= ${LionsOS}/libvmm

VMM_IMAGE_DIR := ${EXAMPLE_DIR}/vmm
LINUX := $(VMM_IMAGE_DIR)/Linux
INITRD := $(VMM_IMAGE_DIR)/initrd.img

IMAGES := vmm.elf
CFLAGS := \
	-mtune=$(CPU) \
	-mstrict-align \
	-ffreestanding \
	-g \
	-O0 \
	-Wall \
	-Wno-unused-function \
	-I. \
	-I$(BOARD_DIR)/include \
	-target $(TARGET) \
	-I$(LIBVMM_DIR)/src/arch/aarch64 \
	-I$(LIBVMM_DIR)/src \
	-I$(LIBVMM_DIR)/src/util \
	-DBOARD_$(MICROKIT_BOARD) \
	-I$(SDDF)/include \
	-MD \
	-DMAC_BASE_ADDRESS=$(MAC_BASE_ADDRESS)

VPATH:=${LIBVMM_DIR}:${VMM_IMAGE_DIR}

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld

IMAGE_FILE := $(BUILD_DIR)/vmdev.img
REPORT_FILE := $(BUILD_DIR)/report.txt

VMM_OBJS := vmm.o  package_guest_images.o
all: $(IMAGE_FILE)

-include vmm.d

%.dtb: %.dts
	$(DTC) -q -I dts -O dtb $< > $@ || rm -f $@

${notdir $(ORIGINAL_DTB:.dtb=.dts)}: ${ORIGINAL_DTB} ${MAKEFILE}
	$(DTC) -q -I dtb -O dts $< > $@ || rm -f $@

dtb.dts: ${notdir $(ORIGINAL_DTB:.dtb=.dts)} ${DT_OVERLAYS} vmm_ram.h ${CHECK_VARIANT}
	${LIBVMM_DIR}/tools/dtscat ${notdir $(ORIGINAL_DTB:.dtb=.dts)} ${DT_OVERLAYS} | cpp -nostdinc -undef -x assembler-with-cpp -P - > $@ || rm -f $@

vmm.o: vmm_ram.h
package_guest_images.o: $(LIBVMM_DIR)/tools/package_guest_images.S  $(LINUX) $(INITRD) dtb.dtb
	$(CC) -c -g3 -x assembler-with-cpp \
					-DGUEST_KERNEL_IMAGE_PATH=\"$(LINUX)\" \
					-DGUEST_DTB_IMAGE_PATH=\"dtb.dtb\" \
					-DGUEST_INITRD_IMAGE_PATH=\"$(INITRD)\" \
					-target $(TARGET) \
					$< -o $@

vmm_ram.h: ${INITRD} ${VMM_IMAGE_DIR}/vmm_ram_input.h ${MAKEFILE}
	size=$$(( (($$(stat -c '%s'  ${INITRD}) + 4095 ) / 4096 ) * 4096 )) ;\
	echo $$size ; \
	start=$$(sed -n 's/.*GUEST_INIT_RAM_DISK_VADDR.*\(0x[0-9a-fA-F]*\).*/\1/p' ${VMM_IMAGE_DIR}/vmm_ram_input.h ) ;\
	echo $$start ;\
	end=$$(printf "0x%x" $$(($$start + $$size)) ) ;\
	echo $$end ;\
	sed "s/INITRD_END .*/INITRD_END $${end}/"  ${VMM_IMAGE_DIR}/vmm_ram_input.h > $@

vmm.elf: ${VMM_OBJS} libvmm.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

vmm.system: ${SYSTEM} vmm_ram.h ${MAKEFILE} ${CHECK_VARIANT}
	cpp -I. -P ${EXAMPLE_DIR}/vmm.system -o $@

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) vmm.system
	$(MICROKIT_TOOL) vmm.system --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

FORCE:

clean::
	rm -f ${VMM_OBJS}

clobber:: clean
	rm -f vmm.elf vmm.system libvmm.a *.d dtb.dtb dtb.ds ${REPORT} ${IMAGE}

# How to build libvmm.a
include ${LIBVMM_DIR}/vmm.mk
