#
# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
#
ifeq ($(strip $(LIBC)),)
$(error "LIBC must be defined")
endif
ifeq ($(strip $(LIBGCC)),)
$(error "LIBGCC must be defined")
endif
ifeq ($(strip $(CPU)),)
$(error "CPU must be defined")
endif
ifeq ($(strip $(CROSS_COMPILE)),)
$(error "CROSS_COMPILE must be defined")
endif

CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld

LIONSOS := $(abspath ../../)
SDDF := $(LIONSOS)/dep/sddf
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco
LWIP := $(SDDF)/network/ipstacks/lwip
# Path to micropython source code
MICROPYTHON_SRC := $(LIONSOS)/dep/micropython
FROZEN_MANIFEST ?= $(MICROPYTHON_SRC)/extmod/asyncio/manifest.py

# Include the core environment definitions; this will set $(TOP).
include $(MICROPYTHON_SRC)/py/mkenv.mk

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
	-DLWIP_TCP_KEEPALIVE=1 \
	-ffreestanding \
	-mtune=$(CPU) \
	-DMPZ_DIG_SIZE=16

LDFLAGS += -L$(BOARD_DIR)/lib -L$(LIBGCC) -L$(LIBC) -L$(LIBMATH)
LIBS += -lm -lc -lgcc -lmicrokit -Tmicrokit.ld -lc $(BUILD)/libsddf_util_debug.a

# Include libmicrokitco for blocking in Micropython
BUILD_DIR=$(BUILD)
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco
LIBMICROKITCO_OPT_PATH := $(LIONSOS)/components/micropython
LIBMICROKITCO_OBJ := $(BUILD)/libmicrokitco.a

include $(LIBMICROKITCO_PATH)/libmicrokitco.mk

# Define the required source files.
SRC_C = \
	micropython.c \
	mphalport.c \
	modtime.c \
	modfs_raw.c \
	vfs_fs_file.c \
	vfs_fs.c \
	fs_helpers.c \
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
	shared/runtime/sys_stdio_mphal.c \
	shared/libc/string0.c \
	shared/libc/printf.c \
	shared/libc/__errno.c \
	shared/timeutils/timeutils.c \
	shared/netutils/netutils.c

# Define source files containung qstrs.
SRC_QSTR += shared/readline/readline.c shared/runtime/pyexec.c vfs_fs_file.c vfs_fs.c modfs_raw.c

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

all: $(BUILD)/micropython.elf

$(BUILD)/micropython.elf: $(OBJ) $(LIBMICROKITCO_OBJ) $(BUILD)/lib_sddf_lwip_mp.a
	$(ECHO) "LINK $@"
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@
	$(Q)$(SIZE) $@

# Include remaining core make rules.
include $(TOP)/py/mkrules.mk
