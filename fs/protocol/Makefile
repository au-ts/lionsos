#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

BUILD_DIR ?= .

CC := clang
TARGET := aarch64-none-elf

LIONS_OS := ../../

CFLAGS := \
	-target $(TARGET) \
	-mstrict-align \
	-ffreestanding \
	-I$(LIONS_OS)/include

all: $(BUILD_DIR)/sddf_fs_protocol.o

$(BUILD_DIR)/sddf_fs_protocol.o: protocol.c $(LIONS_OS)/include/lions_os/fs/protocol.h
	$(CC) -c $(CFLAGS) protocol.c -o $(BUILD_DIR)/sddf_fs_protocol.o

.PHONY: clean
clean:
	rm $(BUILD_DIR)/sddf_fs_protocol.o
