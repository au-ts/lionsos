#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This Makefile snippet builds the POSIX library
#
# NOTES:
# Requires variables
#	CC
#	AR
#	RANLIB
#	LIB_POSIX_LIBC_INCLUDE

LIB_POSIX_DIR := $(LIONSOS)/lib/posix

LIB_POSIX_CFLAGS := -I$(LIB_POSIX_LIBC_INCLUDE) \
					-I$(LIB_POSIX_DIR)/lwip_include \
					-I$(LWIP)/include \
					-I$(LWIP)/include/ipv4

LIB_SDDF_LWIP_CFLAGS_posix := ${LIB_POSIX_CFLAGS}
include $(SDDF)/network/lib_sddf_lwip/lib_sddf_lwip.mk

LIB_POSIX_FILES := posix.c tcp.c
LIB_POSIX_OBJ := $(addprefix lib/posix/, $(LIB_POSIX_FILES:.c=.o))

lib_posix.a: $(LIB_POSIX_OBJ)
	$(AR) crv $@ $^
	$(RANLIB) $@

$(LIB_POSIX_OBJ): CFLAGS += $(LIB_POSIX_CFLAGS)
$(LIB_POSIX_OBJ): $(MUSL)/lib/libc.a
$(LIB_POSIX_OBJ): |lib/posix

lib/posix/%.o: $(LIB_POSIX_DIR)/%.c $(LIB_POSIX_LIBC_INCLUDE)
	$(CC) -c $(CFLAGS) $< -o $@

lib/posix/lwip/%.o: $(LWIP)/%.c
	$(CC) -c $(CFLAGS) $< -o $@

lib/posix:
	mkdir -p $@

-include $(LIB_POSIX_OBJ:.o=.d)
