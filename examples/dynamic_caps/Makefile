#
# Copyright 2026, LionsOS Contributors
#
# SPDX-License-Identifier: BSD-2-Clause
#
# A spike: a manager PD that makes, grants and revokes seL4 objects at run
# time. It needs a Microkit SDK built with microkit-cspace-untyped-cnode-frames.patch
# (see README.md), so it is not part of CI.
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must point to a Microkit SDK with the patch applied)
endif

MICROKIT_BOARD ?= qemu_virt_aarch64
MICROKIT_CONFIG ?= debug
BUILD_DIR ?= build

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit

ifneq ($(MICROKIT_BOARD),qemu_virt_aarch64)
$(error Only qemu_virt_aarch64 is supported)
endif

CC := clang -target aarch64-none-elf
LD := ld.lld
OBJCOPY := llvm-objcopy

CFLAGS := -nostdlib -ffreestanding -g -O2 -Wall -Wno-unused-function -Werror -mstrict-align -I$(BOARD_DIR)/include
LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld

IMAGE_FILE := $(BUILD_DIR)/loader.img
REPORT_FILE := $(BUILD_DIR)/report.txt

all: $(IMAGE_FILE)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: %.c layout.h Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

# The program is a flat binary, linked where the manager maps it in the sandbox
$(BUILD_DIR)/program.elf: $(BUILD_DIR)/program.o program.ld
	$(LD) -T program.ld $< -o $@

$(BUILD_DIR)/program.bin: $(BUILD_DIR)/program.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/program_blob.o: program_blob.S $(BUILD_DIR)/program.bin
	$(CC) -c -DPROGRAM_BIN=\"$(BUILD_DIR)/program.bin\" $< -o $@

$(BUILD_DIR)/manager.elf: $(BUILD_DIR)/manager.o $(BUILD_DIR)/program_blob.o
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/sandbox.elf: $(BUILD_DIR)/sandbox.o
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/display.elf: $(BUILD_DIR)/display.o
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(IMAGE_FILE) $(REPORT_FILE): $(BUILD_DIR)/manager.elf $(BUILD_DIR)/sandbox.elf $(BUILD_DIR)/display.elf \
		dynamic_caps.system
	$(MICROKIT_TOOL) dynamic_caps.system --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) \
		--config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

qemu: $(IMAGE_FILE)
	qemu-system-aarch64 -machine virt,virtualization=on -cpu cortex-a53 -m size=2G \
		-serial mon:stdio -nographic \
		-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all qemu clean
