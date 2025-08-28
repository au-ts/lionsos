#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This Makefile snippet builds the MicroPython component
#
# NOTES:
# Generates micropython.elf
# Requires variables:
#	MICROPYTHON_LIBMATH
# Optional variables:
#	MICROPYTHON_FROZEN_MANIFEST
#	MICROPYTHON_EXEC_MODULE
#	MICROPYTHON_ENABLE_FRAMEBUFFER
#	MICROPYTHON_ENABLE_VFS_STDIO
#	MICROPYTHON_ENABLE_SERIAL_STDIO
#   MICROPYTHON_USER_C_MODULES (assumed to be located in MICROPYTHON_DIR)

MICROPYTHON_DIR := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))

ifneq ($(strip $(MICROPYTHON_USER_C_MODULES)),)
	MICROPYTHON_USER_C_MODULES_PATH := $(MICROPYTHON_DIR)/$(MICROPYTHON_USER_C_MODULES)
endif

MICROPYTHON_GCC_LIBC_INCLUDE :=  $(dir $(realpath $(shell aarch64-none-elf-gcc --print-file-name libc.a)))/../include

LIB_SDDF_LWIP_CFLAGS_mp := \
	-I$(MICROPYTHON_GCC_LIBC_INCLUDE) \
	-I$(MICROPYTHON_DIR)/lwip_include \
	-I$(SDDF)/network/ipstacks/lwip/src/include \
	-Wno-tautological-constant-out-of-range-compare

include $(SDDF)/network/lib_sddf_lwip/lib_sddf_lwip.mk

micropython.elf: FORCE mpy-cross ${LIONSOS}/dep/libmicrokitco/Makefile $(MICROPYTHON_FROZEN_MANIFEST) $(MICROPYTHON_EXEC_MODULE) $(MICROPYTHON_USER_C_MODULES_PATH) lib_sddf_lwip_mp.a
	$(MAKE) -C $(MICROPYTHON_DIR) \
		-j$(nproc) \
		MICROKIT_SDK=$(MICROKIT_SDK) \
		MICROKIT_BOARD=$(MICROKIT_BOARD) \
		MICROKIT_CONFIG=$(MICROKIT_CONFIG) \
		MICROPY_MPYCROSS=$(abspath mpy_cross/mpy-cross) \
		MICROPY_MPYCROSS_DEPENDENCY=$(abspath mpy_cross/mpy-cross) \
		BUILD=$(abspath .) \
		LIBMATH=$(MICROPYTHON_LIBMATH) \
		FROZEN_MANIFEST=$(abspath $(MICROPYTHON_FROZEN_MANIFEST)) \
		EXEC_MODULE=$(MICROPYTHON_EXEC_MODULE) \
		ENABLE_FRAMEBUFFER=$(MICROPYTHON_ENABLE_FRAMEBUFFER) \
		ENABLE_VFS_STDIO=$(MICROPYTHON_ENABLE_VFS_STDIO) \
		ENABLE_SERIAL_STDIO=$(MICROPYTHON_ENABLE_SERIAL_STDIO) \
		USER_C_MODULES=$(MICROPYTHON_USER_C_MODULES)

mpy-cross: FORCE $(LIONSOS)/dep/micropython/mpy-cross
	make -C $(LIONSOS)/dep/micropython/mpy-cross BUILD=$(abspath ./mpy_cross)

FORCE: ;
