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
# Generates fat.elf and fat_mux.elf

FAT_SRC_DIR := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
FAT_FF15_SRC_DIR := $(LIONSOS)/dep/ff15

FAT_CFLAGS := \
	-I$(FAT_LIBC_INCLUDE) \
	-I$(LIBMICROKITCO_PATH) \
	-I$(FAT_FF15_SRC_DIR) \
	-I$(FAT_SRC_DIR)/config

LIBMICROKITCO_CFLAGS_fat := ${FAT_CFLAGS}

FAT_COMMON_OBJ := \
	fat/ff15/ff.o \
	fat/ff15/ffunicode.o \
	fat/io.o

FAT_OBJ := \
	$(FAT_COMMON_OBJ) \
	fat/event.o \
	fat/op.o

CHECK_FAT_FLAGS_MD5 := .fat_cflags-$(shell echo -- $(CFLAGS) $(FAT_CFLAGS) | shasum | sed 's/ *-//')

$(CHECK_FAT_FLAGS_MD5):
	-rm -f .fat_cflags-*
	touch $@

LIB_FS_SERVER_LIBC_INCLUDE := $(FAT_LIBC_INCLUDE)
include $(LIONSOS)/lib/fs/server/lib_fs_server.mk

fat:
	mkdir -p fat

fat/ff15:
	mkdir -p fat/ff15

fat/ff15/%.o: CFLAGS := $(FAT_CFLAGS) \
						$(CFLAGS)
fat/ff15/%.o: $(FAT_FF15_SRC_DIR)/%.c $(FAT_LIBC_INCLUDE) $(CHECK_FAT_FLAGS_MD5) |fat/ff15
	$(CC) -c $(CFLAGS) $< -o $@

fat/%.o: CFLAGS += $(FAT_CFLAGS)
fat/%.o: $(FAT_SRC_DIR)/%.c $(FAT_LIBC_INCLUDE) $(CHECK_FAT_FLAGS_MD5) |fat
	$(CC) -c $(CFLAGS) $< -o $@

fat.elf: $(FAT_OBJ) libmicrokitco_fat.a $(FAT_LIBC_LIB) lib_fs_server.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

# The multiplexer-facing server uses the same FatFs and block I/O
# implementation. Only its FS transport and per-client operation state differ.
FAT_MUX_OBJ := \
	$(FAT_COMMON_OBJ) \
	fat_mux/event.o \
	fat_mux/op.o

fat_mux:
	mkdir -p $@

fat_mux/%.o: CFLAGS := $(FAT_CFLAGS) $(CFLAGS) -DFS_MULTIPLEXED
fat_mux/%.o: $(FAT_SRC_DIR)/%.c $(FAT_LIBC_INCLUDE) $(CHECK_FAT_FLAGS_MD5) | fat_mux
	$(CC) -c $(CFLAGS) $< -o $@

fat_mux.elf: $(FAT_MUX_OBJ) libmicrokitco_fat.a $(FAT_LIBC_LIB) lib_fs_server.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

-include $(FAT_OBJ:.o=.d) $(FAT_MUX_OBJ:.o=.d)
