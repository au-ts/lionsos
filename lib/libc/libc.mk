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

LIB_C_DIR := $(LIONSOS)/lib/libc

MUSL_SRC := $(LIONSOS)/dep/musllibc
MUSL := musllibc
LIBC := libc
LIONS_LIBC = $(abspath $(LIBC))

CFLAGS += -I$(LIONS_LIBC)/include

LIB_C_POSIX_FILES := $(wildcard $(LIB_C_DIR)/posix/*.c)
LIB_C_POSIX_OBJ := $(addprefix $(LIBC)/posix/, $(notdir $(LIB_C_POSIX_FILES:.c=.o)))

LIB_C_COMPILER_RT_FILES := $(wildcard $(LIB_C_DIR)/compiler_rt/*.c)
LIB_C_COMPILER_RT_OBJ := $(addprefix $(LIBC)/compiler_rt/, $(notdir $(LIB_C_COMPILER_RT_FILES:.c=.o)))

BUILD := $(BUILD_DIR)
LIB_FS_HELPER_OBJ := $(BUILD)/fs/helpers.o

include $(LIONSOS)/lib/fs/helpers/fs_helpers.mk

$(LIBC) $(LIBC)/lib $(LIBC)/posix $(LIBC)/compiler_rt:
	mkdir -p $@

$(LIONS_LIBC)/lib/libc.a: $(MUSL)/lib/libc.a $(LIB_C_POSIX_OBJ) $(LIB_C_COMPILER_RT_OBJ) $(LIB_FS_HELPER_OBJ) | $(LIBC)/lib
	cp $< $@
	$(AR) rcs $@ $(LIB_C_POSIX_OBJ) $(LIB_C_COMPILER_RT_OBJ) $(LIB_FS_HELPER_OBJ)
	$(RANLIB) $@

$(LIBC)/posix/tcp.o: CFLAGS += -I$(LWIP)/include -I$(LIB_C_DIR)/posix/lwip_include

$(LIBC)/posix/%.o: $(LIB_C_DIR)/posix/%.c | $(LIONS_LIBC)/include $(LIBC)/posix
	$(CC) -c $(CFLAGS) $< -o $@

$(LIBC)/compiler_rt/%.o: $(LIB_C_DIR)/compiler_rt/%.c | $(LIONS_LIBC)/include $(LIBC)/compiler_rt
	$(CC) -c $(CFLAGS) $< -o $@

$(MUSL):
	mkdir -p $@

$(MUSL)/lib/libc.a $(LIONS_LIBC)/include: ${MUSL_SRC}/Makefile | $(MUSL) $(LIBC)
	cd ${MUSL} && CC=$(CC) CFLAGS="-target $(TARGET) -mtune=$(CPU)" CROSS_COMPILE=llvm- \
		${MUSL_SRC}/configure --target=$(TARGET) --srcdir=${MUSL_SRC} --prefix=$(MUSL) \
		--includedir=$(LIONS_LIBC)/include --with-malloc=oldmalloc --enable-warnings --disable-shared --enable-static
	${MAKE} -C $(MUSL) install

${MUSL_SRC}/Makefile:
	cd ${LIONSOS}; git submodule update --init dep/musllibc

-include $(LIB_C_OBJ:.o=.d)
