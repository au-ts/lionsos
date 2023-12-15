#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

BUILD_DIR ?= .

CC := clang
TARGET := aarch64-none-elf

CFLAGS := \
	-target $(TARGET) \
	-mstrict-align \
	-ffreestanding \
	-I../include

all: $(BUILD_DIR)/sddf_fs_protocol.o

$(BUILD_DIR)/sddf_fs_protocol.o: protocol.c ../include/fs/protocol.h
	$(CC) -c $(CFLAGS) protocol.c -o $(BUILD_DIR)/sddf_fs_protocol.o

.PHONY: clean
clean:
	rm $(BUILD_DIR)/sddf_fs_protocol.o
