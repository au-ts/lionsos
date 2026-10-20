#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# NOTES:
# Requires variables:
#	LIONSOS
#   SDDF
#
# Optional variables:
#   LIBS (appended to)
#   LDFLAGS (appended to)
#   LIBMATH (passed to micropython)
#
# Transitively used variables:
#	WASM_TARGETS (used for WASM)
#   LIB_SDDF_LWIP_CFLAGS
#   LIBMICROKITCO_CFLAGS_????
#   MICROPYTHON_EXEC_MODULE
#   MICROPYTHON_FROZEN_MANIFEST
#

LWIP := $(SDDF)/network/ipstacks/lwip/src
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco

CFLAGS := $(CFLAGS) \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(LIBMICROKITCO_PATH) \
	-I$(LWIP)/include

# Generates LIONS_LIBC
include $(LIONSOS)/lib/libc/libc.mk

LDFLAGS := $(LDFLAGS) -L$(LIONS_LIBC)/lib
LIBS := $(LIBS) -lc

SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(SDDF)/tools/make/sddf.mk

include $(SDDF)/libco/libco.mk

MICROPYTHON_LIBMATH := $(LIBMATH)
include $(LIONSOS)/components/micropython/micropython.mk

FAT_LIBC_LIB := $(LIONS_LIBC)/lib/libc.a
FAT_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIONSOS)/components/fs/fat/fat.mk

include $(LIONSOS)/components/fs/nfs/nfs.mk

include $(LIONSOS)/components/wamr/wamr.mk

LIBMICROKITCO_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIBMICROKITCO_PATH)/libmicrokitco.mk
