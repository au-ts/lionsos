ifeq ($(strip $(ETHERNET_CONFIG_INCLUDE)),)
$(error ETHERNET_CONFIG_INCLUDE must be specified)
endif

CC := aarch64-none-elf-gcc
LD := aarch64-none-elf-ld

LIONSOS := ../../../..
SDDF := $(LIONSOS)/sddf
SDDF_UTIL := $(SDDF)/util
LIBCO := $(LIONSOS)/libco
LWIP := $(SDDF)/network/ipstacks/lwip
# Path to micropython source code
MICROPYTHON_SRC := $(LIONSOS)/micropython
FROZEN_MANIFEST := $(LIONSOS)/micropython/extmod/asyncio/manifest.py

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
	-I. \
	-Ilwip_include \
	-I$(BUILD) \
	-I$(TOP) \
	-I$(SDDF) \
	-I$(SDDF)/include \
	-I$(BOARD_DIR)/include \
	-I$(LIBCO) \
	-I$(LIONSOS)/fs/include \
	-I$(LWIP)/src/include \
	-I$(LWIP)/src/include/ipv4 \
	-I$(ETHERNET_CONFIG_INCLUDE) \
	-DLWIP_TCP_KEEPALIVE=1 \
	-ffreestanding \
	-mtune=$(CPU)
LDFLAGS += -L$(BOARD_DIR)/lib -L$(SDDF)/lib -L$(LIBMATH)
LIBS += -lm -lmicrokit -Tmicrokit.ld -lc

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
	modmyport.c \
	modtime.c \
	modmachine.c \
	modfb.c \
	machine_i2c.c \
	mpnetworkport.c \
	modfs_raw.c \
	vfs_sddf_fs_file.c \
	vfs_sddf_fs.c \
	fs_helpers.c \
	frozen_content.c \
	extmod/modtime.c \
	extmod/vfs.c \
	extmod/modos.c \
	extmod/vfs_reader.c \
	extmod/machine_i2c.c \
	extmod/modasyncio.c \
	extmod/modselect.c \
	extmod/modlwip.c \
	extmod/modre.c \
	extmod/moddeflate.c \
	extmod/modframebuf.c \
	extmod/moductypes.c \
	extmod/modjson.c \
	shared/readline/readline.c \
	shared/runtime/gchelper_generic.c \
	shared/runtime/pyexec.c \
	shared/runtime/stdout_helpers.c \
	shared/libc/string0.c \
	shared/libc/printf.c \
	shared/libc/__errno.c \
	shared/netutils/netutils.c \
	$(LIBCO)/libco.c \
	$(SDDF_UTIL)/printf.c \
	$(SDDF_UTIL)/putchar_debug.c \

# Define source files containung qstrs.
SRC_QSTR += shared/readline/readline.c shared/runtime/pyexec.c modmyport.c modfb.c vfs_sddf_fs_file.c vfs_sddf_fs.c modmachine.c machine_i2c.c modfs_raw.c

# Define the required object files.
OBJ = $(PY_CORE_O) $(addprefix $(BUILD)/, $(SRC_C:.c=.o)) $(LWIP_OBJ) $(BUILD)/sddf_fs_protocol.o $(BUILD)/sddf_util_cache.o

all: directories $(BUILD)/micropython.elf

$(BUILD)/micropython/lwip/%.o: $(LWIP)/src/%.c
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD)/micropython.elf: $(OBJ)
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
