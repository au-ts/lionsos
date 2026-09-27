# SPDX-FileCopyrightText: 2026 UNSW
# SPDX-License-Identifier: BSD-2-Clause

FS_MULTIPLEXER_LIBC_INCLUDE ?= $(LIONS_LIBC)/include
FS_MULTIPLEXER_LIBC_LIB ?= $(LIONS_LIBC)/lib/libc.a

FS_MULTIPLEXER_DIR := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
FS_MULTIPLEXER_CFLAGS := \
	-I$(LIONSOS)/include \
	-I$(FS_MULTIPLEXER_LIBC_INCLUDE) \
	-DFS_MULTIPLEXED

fs_multiplexer:
	mkdir -p fs_multiplexer

fs_multiplexer/multiplexer.o: CFLAGS := $(CFLAGS) $(FS_MULTIPLEXER_CFLAGS)
fs_multiplexer/multiplexer.o: $(FS_MULTIPLEXER_DIR)/multiplexer.c | fs_multiplexer
	$(CC) -c $(CFLAGS) $< -o $@

fs_multiplexer.elf: fs_multiplexer/multiplexer.o $(FS_MULTIPLEXER_LIBC_LIB)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

-include fs_multiplexer/multiplexer.d
