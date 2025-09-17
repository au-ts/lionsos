#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This Makefile snippet builds the FS server library
#
# NOTES:
# Requires variables
#	CC
#	AR
#	RANLIB
# 	LIBFS_LIBC_INCLUDE

LIB_FS_SERVER_DIR := $(LIONSOS)/lib/fs/server
LIB_FS_SERVER_OBJ := $(addprefix lib/fs/server/, fd.o memory.o)

LIB_FS_SERVER_CFLAGS := -I$(LIB_FS_SERVER_LIBC_INCLUDE)

lib_fs_server.a: $(LIB_FS_SERVER_OBJ)
	$(AR) crv $@ $^
	$(RANLIB) $@

lib/fs/server/%.o: CFLAGS += $(LIB_FS_SERVER_CFLAGS)
lib/fs/server/%.o: $(LIB_FS_SERVER_DIR)/%.c $(LIB_FS_SERVER_LIBC_INCLUDE) |lib/fs/server $(LIONS_LIBC)/include
	$(CC) -c $(CFLAGS) $< -o $@

lib/fs/server:
	mkdir -p $@

-include $(LIB_FS_SERVER_OBJ:.o=.d)
