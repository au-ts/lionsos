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
#	LIB_FP_LIBC_INCLUDE

LIB_FP_DIR := $(LIONSOS)/lib/fp

LIB_FP_CFLAGS := -I$(LIB_FP_LIBC_INCLUDE)

LIB_FP_FILES := addtf3.c \
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
LIB_FP_OBJ := $(addprefix lib/fp/, $(LIB_FP_FILES:.c=.o))

lib_fp.a: $(LIB_FP_OBJ)
	$(AR) crv $@ $^
	$(RANLIB) $@

$(LIB_FP_OBJ): CFLAGS += $(LIB_FP_CFLAGS)
$(LIB_FP_OBJ): $(MUSL)/lib/libc.a
$(LIB_FP_OBJ): |lib/fp

lib/fp/%.o: $(LIB_FP_DIR)/%.c $(LIB_FP_LIBC_INCLUDE)
	$(CC) -c $(CFLAGS) $< -o $@

lib/fp:
	mkdir -p $@

-include $(LIB_FP_OBJ:.o=.d)
