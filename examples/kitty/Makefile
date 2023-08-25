#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(SEL4CP_SDK)),)
$(error SEL4CP_SDK must be specified)
endif

SEL4CP_CONFIG ?= debug
BUILD_DIR ?= $(abspath build)
SEL4CP_BOARD := odroidc4
CPU := cortex-a55

TOOLCHAIN := clang
CC := clang
LD := ld.lld
TARGET := aarch64-none-elf
SEL4CP_TOOL ?= $(SEL4CP_SDK)/bin/sel4cp
DTC := dtc

BOARD_DIR := $(SEL4CP_SDK)/board/$(SEL4CP_BOARD)/$(SEL4CP_CONFIG)
SDDF := ../../sddf
LIBVMM := ../../vmm

VMM_IMAGE_DIR := src/vmm/images
LINUX := $(VMM_IMAGE_DIR)/linux
INITRD := $(VMM_IMAGE_DIR)/rootfs.cpio.gz
DTS := $(VMM_IMAGE_DIR)/linux.dts
DTB := $(BUILD_DIR)/linux.dtb

IMAGES := main.elf micropython.elf timer.elf vmm.elf
CFLAGS := -mtune=$(CPU) -mstrict-align -ffreestanding -g3 -O3 -Wall -Wno-unused-function -I$(BOARD_DIR)/include -target $(TARGET) \
		  -I$(LIBVMM)/src/arch/aarch64 -I$(LIBVMM)/src -I$(LIBVMM)/src/util -DBOARD_$(SEL4CP_BOARD)
LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lsel4cp -Tsel4cp.ld

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
			SEL4CP_SDK=$(SEL4CP_SDK) \
			SEL4CP_BOARD=$(SEL4CP_BOARD) \
			SEL4CP_CONFIG=$(SEL4CP_CONFIG) \
			BUILD=$(BUILD_DIR) \
			PACKAGE_PYTHON_SCRIPTS=package_python_scripts.o

$(IMAGE_FILE) $(REPORT_FILE): $(addprefix $(BUILD_DIR)/, $(IMAGES)) kitty.system
	$(SEL4CP_TOOL) kitty.system --search-path $(BUILD_DIR) --board $(SEL4CP_BOARD) --config $(SEL4CP_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)
