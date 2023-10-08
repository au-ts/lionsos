#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif

MICROKIT_CONFIG ?= debug
BUILD_DIR ?= $(abspath build)
MICROKIT_BOARD := odroidc4
CPU := cortex-a55

TOOLCHAIN := clang
CC := clang
LD := ld.lld
TARGET := aarch64-none-elf
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
DTC := dtc

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
SDDF := ../../sddf
LIBVMM := ../../vmm

VMM_IMAGE_DIR := src/vmm/images
LINUX := $(VMM_IMAGE_DIR)/linux
INITRD := $(VMM_IMAGE_DIR)/rootfs.cpio.gz
DTS := $(VMM_IMAGE_DIR)/linux.dts
DTB := $(BUILD_DIR)/linux.dtb

IMAGES := main.elf micropython.elf timer.elf vmm.elf
CFLAGS := -mtune=$(CPU) -mstrict-align -ffreestanding -g3 -O3 -Wall -Wno-unused-function -I$(BOARD_DIR)/include -target $(TARGET) \
		  -I$(LIBVMM)/src/arch/aarch64 -I$(LIBVMM)/src -I$(LIBVMM)/src/util -DBOARD_$(MICROKIT_BOARD)
LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld

IMAGE_FILE := $(BUILD_DIR)/kitty.img
REPORT_FILE := $(BUILD_DIR)/report.txt

MAIN_OBJS := main.o
TIMER_OBJS := timer.o
VMM_OBJS := vmm.o printf.o virq.o linux.o \
			guest.o psci.o smc.o fault.o \
			util.o vgic.o vgic_v2.o \
			package_guest_images.o tcb.o vcpu.o

TIMER_DRIVER := $(SDDF)/drivers/clock/meson

PACKAGE_PYTHON_SCRIPTS := src/package_python_scripts.S

all: $(BUILD_DIR) $(IMAGE_FILE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: src/vmm/%.c
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/timer.o: $(TIMER_DRIVER)/timer.c
	$(CC) -c $(CFLAGS) $< -o $@

$(DTB): $(DTS)
	$(DTC) -q -I dts -O dtb $< > $@

$(BUILD_DIR)/package_guest_images.o: $(LIBVMM)/tools/package_guest_images.S $(VMM_IMAGE_DIR) $(LINUX) $(INITRD) $(DTB)
	$(CC) -c -g3 -x assembler-with-cpp \
					-DGUEST_KERNEL_IMAGE_PATH=\"$(LINUX)\" \
					-DGUEST_DTB_IMAGE_PATH=\"$(DTB)\" \
					-DGUEST_INITRD_IMAGE_PATH=\"$(INITRD)\" \
					-target $(TARGET) \
					$< -o $@

$(BUILD_DIR)/%.o: $(LIBVMM)/src/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(LIBVMM)/src/util/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(LIBVMM)/src/arch/aarch64/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(LIBVMM)/src/arch/aarch64/vgic/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/vmm.elf: $(addprefix $(BUILD_DIR)/, $(VMM_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/main.elf: $(addprefix $(BUILD_DIR)/, $(MAIN_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/timer.elf: $(addprefix $(BUILD_DIR)/, $(TIMER_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/package_python_scripts.o: $(PACKAGE_PYTHON_SCRIPTS) src/kitty.py
	$(CC) -c -g3 -x assembler-with-cpp \
				-DKITTY_PYTHON_SCRIPT_PATH=\"src/kitty.py\" \
				-target $(TARGET) \
				$< -o $@

$(BUILD_DIR)/micropython.elf:$(BUILD_DIR)/package_python_scripts.o
	make -C src/micropython -j$(nproc) \
			MICROKIT_SDK=$(MICROKIT_SDK) \
			MICROKIT_BOARD=$(MICROKIT_BOARD) \
			MICROKIT_CONFIG=$(MICROKIT_CONFIG) \
			BUILD=$(BUILD_DIR) \
			PACKAGE_PYTHON_SCRIPTS=package_python_scripts.o

$(IMAGE_FILE) $(REPORT_FILE): $(addprefix $(BUILD_DIR)/, $(IMAGES)) kitty.system
	$(MICROKIT_TOOL) kitty.system --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)
