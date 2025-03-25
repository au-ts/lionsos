#
# Copyright 2024, UNSW
# SPDX-License-Identifier: BSD-2-Clause
#
LIBC := $(dir $(realpath $(shell aarch64-none-elf-gcc --print-file-name libc.a)))
CC := aarch64-none-elf-gcc
LD := aarch64-none-elf-ld

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

CROSS_COMPILE ?= aarch64-none-elf-

CPU ?= cortex-a55
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
	-mtune=$(CPU)
LDFLAGS += -L$(BOARD_DIR)/lib -L$(LIBC) -L$(LIBMATH)
LIBS += -lm -lmicrokit -Tmicrokit.ld -lc $(BUILD)/libsddf_util_debug.a

# Include libmicrokitco for blocking in Micropython
BUILD_DIR=$(BUILD)
TARGET=aarch64-none-elf
LLVM=1
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco
LIBMICROKITCO_OPT_PATH := $(LIONSOS)/components/micropython
LIBMICROKITCO_OBJ := $(BUILD_DIR)/libmicrokitco/libmicrokitco.a

export LIBMICROKITCO_PATH TARGET MICROKIT_SDK BUILD_DIR MICROKIT_BOARD MICROKIT_CONFIG CPU LLVM
$(LIBMICROKITCO_OBJ):
	make -f $(LIBMICROKITCO_PATH)/Makefile LIBMICROKITCO_OPT_PATH=$(LIBMICROKITCO_OPT_PATH)

# COREFILES, CORE4FILES: The minimum set of files needed for lwIP.
COREFILES=core/init.c \
	core/def.c \
	core/dns.c \
	core/inet_chksum.c \
	core/ip.c \
	core/mem.c \
	core/memp.c \
	core/netif.c \
	core/pbuf.c \
	core/raw.c \
	core/stats.c \
	core/sys.c \
	core/altcp.c \
	core/altcp_alloc.c \
	core/altcp_tcp.c \
	core/tcp.c \
	core/tcp_in.c \
	core/tcp_out.c \
	core/timeouts.c \
	core/udp.c

CORE4FILES=core/ipv4/autoip.c \
	core/ipv4/dhcp.c \
	core/ipv4/etharp.c \
	core/ipv4/icmp.c \
	core/ipv4/igmp.c \
	core/ipv4/ip4_frag.c \
	core/ipv4/ip4.c \
	core/ipv4/ip4_addr.c

# NETIFFILES: Files implementing various generic network interface functions
NETIFFILES=netif/ethernet.c

# LWIPFILES: All the above.
LWIPFILES=$(COREFILES) $(CORE4FILES) $(NETIFFILES)
LWIP_OBJ=$(addprefix $(BUILD)/micropython/lwip/, $(LWIPFILES:.c=.o))

# Define the required source files.
SRC_C = \
	micropython.c \
	mphalport.c \
	modtime.c \
	mpnetworkport.c \
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
OBJ = $(PY_CORE_O) $(addprefix $(BUILD)/, $(SRC_C:.c=.o)) $(LWIP_OBJ)

all: directories $(BUILD)/micropython.elf

$(BUILD)/micropython/lwip/%.o: $(LWIP)/src/%.c
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD)/micropython.elf: $(OBJ) $(LIBMICROKITCO_OBJ)
	$(ECHO) "LINK $@"
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@
	$(Q)$(SIZE) $@

# Include remaining core make rules.
include $(TOP)/py/mkrules.mk

directories:
	$(shell mkdir -p $(BUILD)/micropython/lwip/)
	$(shell mkdir -p $(BUILD)/micropython/lwip/core)
	$(shell mkdir -p $(BUILD)/micropython/lwip/netif)
	$(shell mkdir -p $(BUILD)/micropython/lwip/core/ipv4)
