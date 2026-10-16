#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This Makefile snippet builds the FAT component
#
# NOTES:
# Requires variables:
#	LIONSOS
#	TARGET
#	MICROKIT_SDK
#	MICROKIT_BOARD
#	MICROKIT_CONFIG
#	CPU
#	FAT_LIBC_INCLUDE
#	FAT_LIBC_LIB
# Generates fat.elf

RAMFS_SRC_DIR := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
RAMFS_FF15_SRC_DIR := $(LIONSOS)/dep/ff15

RAMFS_CFLAGS := \
	-I$(RAMFS_LIBC_INCLUDE) \
	-I$(RAMFS_FF15_SRC_DIR) \
	-I$(RAMFS_SRC_DIR)/config

RAMFS_OBJ := \
	ramfs/ff15/ff.o \
	ramfs/ff15/ffunicode.o \
	ramfs/op.o \
	ramfs/io.o

CHECK_RAMFS_FLAGS_MD5 := .ramfs_cflags-$(shell echo -- $(CFLAGS) $(RAMFS_CFLAGS) | shasum | sed 's/ *-//')

$(CHECK_RAMFS_FLAGS_MD5):
	-rm -f .ramfs_cflags-*
	touch $@

LIB_FS_SERVER_LIBC_INCLUDE := $(RAMFS_LIBC_INCLUDE)
include $(LIONSOS)/lib/fs/server/lib_fs_server.mk

ramfs:
	mkdir -p ramfs

ramfs/ff15:
	mkdir -p ramfs/ff15

ramfs/ff15/%.o: CFLAGS := $(RAMFS_CFLAGS) \
						$(CFLAGS)
ramfs/ff15/%.o: $(RAMFS_FF15_SRC_DIR)/%.c $(RAMFS_LIBC_INCLUDE) $(CHECK_RAMFS_FLAGS_MD5) |ramfs/ff15
	$(CC) -c $(CFLAGS) $< -o $@

ramfs/%.o: CFLAGS += $(RAMFS_CFLAGS)
ramfs/%.o: $(RAMFS_SRC_DIR)/%.c $(RAMFS_LIBC_INCLUDE) $(CHECK_RAMFS_FLAGS_MD5) |ramfs
	$(CC) -c $(CFLAGS) $< -o $@

# The fd table from lib/fs/server goes into the same archive, so a client links one library.
ramfs.a: $(RAMFS_OBJ) $(LIB_FS_SERVER_OBJ)
	$(AR) crv $@ $^
	$(RANLIB) $@

-include $(RAMFS_OBJ:.o=.d)
