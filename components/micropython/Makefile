#
# Copyright 2024, UNSW
# SPDX-License-Identifier: BSD-2-Clause
#

LIONSOS := $(abspath ../../)
SDDF := $(LIONSOS)/dep/sddf
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco
LWIP := $(SDDF)/network/ipstacks/lwip
# Path to micropython source code
MICROPYTHON_SRC := $(LIONSOS)/dep/micropython
FROZEN_MANIFEST ?= $(MICROPYTHON_SRC)/extmod/asyncio/manifest.py

# Include the core environment definitions; this will set $(TOP).
include $(MICROPYTHON_SRC)/py/mkenv.mk

# Override mkenv.mk settings.
CC = $(USE_CC)
LD = $(USE_LD)
SIZE := $(USE_SIZE)

# Include py core make definitions.
include $(TOP)/py/py.mk
include $(TOP)/extmod/extmod.mk

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)

# Set CFLAGS and libraries.
CFLAGS += \
	-O2 \
	-I. \
	-Ilwip_include \
	-I$(BUILD) \
	-I$(TOP) \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(BOARD_DIR)/include \
	-I$(LIBMICROKITCO_PATH) \
	-I$(LIONSOS)/include \
	-I$(LWIP)/src/include \
	-I$(LWIP)/src/include/ipv4 \
	-I$(MICROPYTHON_LIBC)/include \
	-Wno-shift-op-parentheses \
	-DLWIP_TCP_KEEPALIVE=1 \
	-ffreestanding \
	-mtune=$(CPU) \
	-target $(TARGET)

LDFLAGS += -L$(BOARD_DIR)/lib -L$(MICROPYTHON_LIBC)/lib
LIBS += -lmicrokit -Tmicrokit.ld -lc $(BUILD)/libsddf_util_debug.a $(BUILD)/lib_sddf_lwip_mp.a

# Include libmicrokitco for blocking in Micropython
BUILD_DIR=$(BUILD)
LLVM=1
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco
LIBMICROKITCO_OPT_PATH := $(LIONSOS)/components/micropython
LIBMICROKITCO_OBJ := $(BUILD_DIR)/libmicrokitco/libmicrokitco.a

export LIBMICROKITCO_PATH TARGET MICROKIT_SDK BUILD_DIR MICROKIT_BOARD MICROKIT_CONFIG CPU LLVM
$(LIBMICROKITCO_OBJ):
	make -f $(LIBMICROKITCO_PATH)/Makefile LIBMICROKITCO_OPT_PATH=$(LIBMICROKITCO_OPT_PATH)

# Define the required source files.
SRC_C = \
	micropython.c \
	mphalport.c \
	modtime.c \
	modinterrupt.c \
	mpfirewallport.c \
	modfs_raw.c \
	vfs_fs_file.c \
	vfs_fs.c \
	fs/helpers.c \
	frozen_content.c \
	extmod/modtime.c \
	extmod/vfs.c \
	extmod/modos.c \
	extmod/vfs_reader.c \
	extmod/modasyncio.c \
	extmod/modselect.c \
	extmod/modlwip.c \
	extmod/modre.c \
	extmod/moddeflate.c \
	extmod/moductypes.c \
	extmod/modjson.c \
	shared/readline/readline.c \
	shared/runtime/gchelper_generic.c \
	shared/runtime/pyexec.c \
	shared/runtime/stdout_helpers.c \
	shared/runtime/interrupt_char.c \
	shared/runtime/sys_stdio_mphal.c \
	shared/timeutils/timeutils.c \
	shared/netutils/netutils.c \
	$(MICROPYTHON_USER_C_MODULES)

# Define source files containung qstrs.
SRC_QSTR += shared/readline/readline.c shared/runtime/pyexec.c vfs_fs_file.c vfs_fs.c modfs_raw.c $(MICROPYTHON_USER_C_MODULES)

# Frozen module to execute; otherwise run REPL
ifdef EXEC_MODULE
CFLAGS += -DEXEC_MODULE=\"$(EXEC_MODULE)\"
endif

# I2C setup
SRC_C += modmachine.c machine_i2c.c extmod/machine_i2c.c
SRC_QSTR += modmachine.c machine_i2c.c

ifdef ENABLE_FRAMEBUFFER
SRC_C += modfb.c extmod/modframebuf.c
SRC_QSTR += modfb.c
CFLAGS += -DENABLE_FRAMEBUFFER
endif

ifdef ENABLE_VFS_STDIO
CFLAGS += -DENABLE_VFS_STDIO
endif

ifdef ENABLE_SERIAL_STDIO
SRC_C += shared/runtime/sys_stdio_mphal.c
CFLAGS += -DENABLE_SERIAL_STDIO
endif

# Define the required object files.
OBJ = $(PY_CORE_O) $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

include $(LIONSOS)/lib/fs/helpers/fs_helpers.mk

all: $(BUILD)/micropython.elf

$(BUILD)/micropython.elf: $(OBJ) $(LIBMICROKITCO_OBJ)
	$(ECHO) "LINK $@"
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@
	$(Q)$(SIZE) $@

# Include remaining core make rules.
include $(TOP)/py/mkrules.mk
