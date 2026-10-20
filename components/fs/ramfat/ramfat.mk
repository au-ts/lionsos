#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This Makefile snippet builds the RAMFAT component
#
# NOTES:
# Requires variables:
#	LIONSOS
#	TARGET
#	MICROKIT_SDK
#	MICROKIT_BOARD
#	MICROKIT_CONFIG
#	CPU
#	RAMFAT_LIBC_INCLUDE
#	RAMFAT_LIBC_LIB
# Generates ramfat.elf

RAMFAT_SRC_DIR := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
RAMFAT_FF15_SRC_DIR := $(LIONSOS)/dep/ff15

RAMFAT_CFLAGS := \
	-I$(RAMFAT_LIBC_INCLUDE) \
	-I$(RAMFAT_FF15_SRC_DIR) \
	-I$(RAMFAT_SRC_DIR)/config

RAMFAT_OBJ := \
	ramfat/ff15/ff.o \
	ramfat/ff15/ffunicode.o \
	ramfat/event.o \
	ramfat/op.o \
	ramfat/io.o

CHECK_RAMFAT_FLAGS_MD5 := .fat_cflags-$(shell echo -- $(CFLAGS) $(RAMFAT_CFLAGS) | shasum | sed 's/ *-//')

$(CHECK_RAMFAT_FLAGS_MD5):
	-rm -f .fat_cflags-*
	touch $@

LIB_FS_SERVER_LIBC_INCLUDE := $(RAMFAT_LIBC_INCLUDE)
include $(LIONSOS)/lib/fs/server/lib_fs_server.mk

ramfat:
	mkdir -p ramfat

ramfat/ff15:
	mkdir -p ramfat/ff15

ramfat/ff15/%.o: CFLAGS := $(RAMFAT_CFLAGS) \
						$(CFLAGS)
ramfat/ff15/%.o: $(RAMFAT_FF15_SRC_DIR)/%.c $(RAMFAT_LIBC_INCLUDE) $(CHECK_RAMFAT_FLAGS_MD5) |ramfat/ff15
	$(CC) -c $(CFLAGS) $< -o $@

ramfat/%.o: CFLAGS += $(RAMFAT_CFLAGS)
ramfat/%.o: $(RAMFAT_SRC_DIR)/%.c $(RAMFAT_LIBC_INCLUDE) $(CHECK_RAMFAT_FLAGS_MD5) |ramfat
	$(CC) -c $(CFLAGS) $< -o $@

ramfat.a: $(RAMFS_OBJ) $(LIB_FS_SERVER_OBJ)
	$(AR) crv $@ $^
	$(RANLIB) $@

-include $(RAMFAT_OBJ:.o=.d)
