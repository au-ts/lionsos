#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This Makefile snippet builds the EXT4 component
#
# NOTES:
# Requires variables:
#	LIONSOS
#	EXT4_LIBC_INCLUDE
#	EXT4_LIBC_LIB
#	LIBMICROKITCO_PATH
# Generates ext4.elf

EXT4_DIR := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
LWEXT4_SRC_DIR := $(LIONSOS)/dep/lwext4

EXT4_CFLAGS := \
	-Wno-unused-function \
	-I$(EXT4_LIBC_INCLUDE) \
	-I$(LIBMICROKITCO_PATH) \
	-I$(LWEXT4_SRC_DIR)/include \
	-I$(EXT4_DIR) \
	-I$(EXT4_DIR)/config \
	-DCONFIG_USE_DEFAULT_CFG=1

LIBMICROKITCO_CFLAGS_ext4 := $(EXT4_CFLAGS)

EXT4_OBJ := \
	ext4/event.o \
	ext4/blockdev.o \
	ext4/op.o

LWEXT4_SRCS := $(wildcard $(LWEXT4_SRC_DIR)/src/*.c)
LWEXT4_OBJS := $(patsubst $(LWEXT4_SRC_DIR)/src/%.c, ext4/lwext4/%.o, $(LWEXT4_SRCS))

CHECK_EXT4_FLAGS_MD5 := .ext4_cflags-$(shell echo -- $(CFLAGS) $(EXT4_CFLAGS) | shasum | sed 's/ *-//')

$(CHECK_EXT4_FLAGS_MD5):
	-rm -f .ext4_cflags-*
	touch $@

LIB_FS_SERVER_LIBC_INCLUDE := $(EXT4_LIBC_INCLUDE)
include $(LIONSOS)/lib/fs/server/lib_fs_server.mk

$(LWEXT4_SRC_DIR)/src/ext4.c $(LWEXT4_SRC_DIR)/include/ext4.h &:
	cd $(LIONSOS); git submodule update --init dep/lwext4

ext4:
	mkdir -p ext4

ext4/lwext4:
	mkdir -p ext4/lwext4

ext4/lwext4/%.o: CFLAGS := $(EXT4_CFLAGS) $(CFLAGS)

ext4/lwext4/%.o: $(LWEXT4_SRC_DIR)/src/%.c $(LWEXT4_SRC_DIR)/include/ext4.h $(CHECK_EXT4_FLAGS_MD5) |ext4/lwext4 $(EXT4_LIBC_INCLUDE)
	$(CC) -c $(CFLAGS) $< -o $@

ext4/%.o: CFLAGS += $(EXT4_CFLAGS)
ext4/%.o: $(EXT4_DIR)/%.c $(LWEXT4_SRC_DIR)/include/ext4.h $(CHECK_EXT4_FLAGS_MD5) |ext4 $(EXT4_LIBC_INCLUDE)
	$(CC) -c $(CFLAGS) $< -o $@

ext4.elf: $(EXT4_OBJ) $(LWEXT4_OBJS) libmicrokitco_ext4.a $(EXT4_LIBC_LIB) lib_fs_server.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

-include $(EXT4_OBJ:.o=.d) $(LWEXT4_OBJS:.o=.d)
