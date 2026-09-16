#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This Makefile snippet builds the pager component
#
# NOTES:
# Requires variables:
#	LIONSOS
#	SDDF
# Generates pager.elf

PAGER_SRC_DIR := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))

PAGER_CFLAGS := \
	-I$(PAGER_SRC_DIR)/include

PAGER_OBJ := \
	pager/bitmap.o \
	pager/cspace.o \
	pager/frame_table.o \
	pager/mem.o \
	pager/page_table.o \
	pager/pager.o \
	pager/proc.o \
	pager/untyped.o

CHECK_PAGER_FLAGS_MD5 := .pager_cflags-$(shell echo -- $(CFLAGS) $(PAGER_CFLAGS) | shasum | sed 's/ *-//')

$(CHECK_PAGER_FLAGS_MD5):
	-rm -f .pager_cflags-*
	touch $@

pager:
	mkdir -p pager

pager/%.o: CFLAGS += $(PAGER_CFLAGS)
pager/%.o: $(PAGER_SRC_DIR)/src/%.c $(CHECK_PAGER_FLAGS_MD5) |pager
	$(CC) -c $(CFLAGS) $< -o $@

pager.elf: $(PAGER_OBJ) libsddf_util_debug.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

-include $(PAGER_OBJ:.o=.d)
