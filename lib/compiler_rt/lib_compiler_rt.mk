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
#	LIB_COMPILER_RT_LIBC_INCLUDE

LIB_COMPILER_RT_DIR := $(LIONSOS)/lib/compiler_rt

LIB_COMPILER_RT_CFLAGS := -I$(LIB_COMPILER_RT_LIBC_INCLUDE)

LIB_COMPILER_RT_FILES := addtf3.c \
				comparetf2.c \
				divtf3.c \
				extenddftf2.c \
				fixtfsi.c \
				fixunstfsi.c \
				floatsitf.c \
				floatunsitf.c \
				fp_mode.c \
				multf3.c \
				subtf3.c
LIB_COMPILER_RT_OBJ := $(addprefix lib/compiler_rt/, $(LIB_COMPILER_RT_FILES:.c=.o))

lib_compiler_rt.a: $(LIB_COMPILER_RT_OBJ)
	$(AR) crv $@ $^
	$(RANLIB) $@

$(LIB_COMPILER_RT_OBJ): CFLAGS += $(LIB_COMPILER_RT_CFLAGS)
$(LIB_COMPILER_RT_OBJ): $(MUSL)/lib/libc.a
$(LIB_COMPILER_RT_OBJ): |lib/compiler_rt

lib/compiler_rt/%.o: $(LIB_COMPILER_RT_DIR)/%.c $(LIB_COMPILER_RT_LIBC_INCLUDE)
	$(CC) -c $(CFLAGS) $< -o $@

lib/compiler_rt:
	mkdir -p $@

-include $(LIB_COMPILER_RT_OBJ:.o=.d)
