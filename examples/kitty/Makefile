#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(SEL4CP_SDK)),)
$(error SEL4CP_SDK must be specified)
endif

SEL4CP_CONFIG ?= debug
BUILD_DIR ?= build
SEL4CP_BOARD := odroidc4_hyp
CPU := cortex-a53

TOOLCHAIN := clang
CC := clang
LD := ld.lld
TARGET := aarch64-none-elf
SEL4CP_TOOL ?= $(SEL4CP_SDK)/bin/sel4cp

BOARD_DIR := $(SEL4CP_SDK)/board/$(SEL4CP_BOARD)/$(SEL4CP_CONFIG)

IMAGES := main.elf
CFLAGS := -mcpu=$(CPU) -mstrict-align -ffreestanding -g3 -O3 -Wall -Wno-unused-function -I$(BOARD_DIR)/include -target $(TARGET)
LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lsel4cp -Tsel4cp.ld

IMAGE_FILE := $(BUILD_DIR)/loader.img
REPORT_FILE := $(BUILD_DIR)/report.txt

MAIN_OBJS := main.o

all: $(BUILD_DIR) $(IMAGE_FILE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/main.elf: $(addprefix $(BUILD_DIR)/, $(MAIN_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(IMAGE_FILE) $(REPORT_FILE): $(addprefix $(BUILD_DIR)/, $(IMAGES)) kitty.system
	$(SEL4CP_TOOL) kitty.system --search-path $(BUILD_DIR) --board $(SEL4CP_BOARD) --config $(SEL4CP_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)
