#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_INCLUDE)),)
$(error MICROKIT_INCLUDE must be specified)
endif

ifeq ($(strip $(MUSLLIBC_INCLUDE)),)
$(error MUSLLIBC_INCLUDE must be specified)
endif

ifeq ($(strip $(LIBNFS_INCLUDE)),)
$(error LIBNFS_INCLUDE must be specified)
endif

ifeq ($(strip $(ETHERNET_CONFIG_INCLUDE)),)
$(error ETHERNET_CONFIG_INCLUDE must be specified)
endif

ifeq ($(strip $(NFS_SERVER)),)
$(error NFS_SERVER must be specified)
endif

ifeq ($(strip $(NFS_DIRECTORY)),)
$(error NFS_DIRECTORY must be specified)
endif

BUILD_DIR ?= .

TOOLCHAIN := aarch64-none-elf
CC := $(TOOLCHAIN)-gcc
TARGET := aarch64-none-elf

LIONSOS := ../../..
LIBNFS := $(LIONSOS)/dep/libnfs
SDDF := $(LIONSOS)/dep/sddf
SDDF_UTIL := $(SDDF)/util
LWIP := $(SDDF)/network/ipstacks/lwip
MUSL := $(LIONSOS)/dep/musllibc/build

INCLUDE := \
	-Ilwip_include \
	-I$(MICROKIT_INCLUDE) \
	-I$(SDDF)/include \
	-I$(MUSLLIBC_INCLUDE) \
	-I$(LIBNFS_INCLUDE) \
	-I$(ETHERNET_CONFIG_INCLUDE) \
	-I$(LWIP)/src/include \
	-I$(LWIP)/src/include/ipv4 \
	-I$(LIONSOS)/include

DEFINE := \
	-DNFS_SERVER="\"$(NFS_SERVER)\"" \
	-DNFS_DIRECTORY="\"$(NFS_DIRECTORY)\"" \
	-DETHERNET_TX_CHANNEL=3 \
	-DETHERNET_RX_CHANNEL=2 \
	-DTIMER_CHANNEL=9 \
	-DCLIENT_CHANNEL=7

CFLAGS := $(INCLUDE) $(DEFINE) \
	-mstrict-align \
	-ffreestanding

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
LWIP_OBJ=$(addprefix $(BUILD_DIR)/lwip/, $(LWIPFILES:.c=.o))

OBJ=\
	$(BUILD_DIR)/nfs.o \
	$(BUILD_DIR)/fd.o \
	$(BUILD_DIR)/op.o \
	$(BUILD_DIR)/posix.o \
	$(BUILD_DIR)/tcp.o \
	$(BUILD_DIR)/printf.o \
	$(BUILD_DIR)/putchar_debug.o \
	$(LWIP_OBJ)

all: directories $(BUILD_DIR)/nfs.a

$(BUILD_DIR)/nfs.a: $(OBJ)
	ar rvs $(BUILD_DIR)/nfs.a $(OBJ)

$(BUILD_DIR)/%.o: %.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/printf.o: $(SDDF_UTIL)/printf.c
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/putchar_debug.o: $(SDDF_UTIL)/putchar_debug.c
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/lwip/%.o: $(LWIP)/src/%.c
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/nfs.o: nfs.c nfs.h tcp.h fd.h util.h
$(BUILD_DIR)/fd.o: fd.c fd.h util.h
$(BUILD_DIR)/op.o: op.c nfs.h fd.h util.h
$(BUILD_DIR)/tcp.o: tcp.c tcp.h util.h
$(BUILD_DIR)/posix.o: posix.c posix.h tcp.h util.h
$(BUILD_DIR)/printf.o: $(SDDF_UTIL)/printf.c
$(BUILD_DIR)/putchar_debug.o: $(SDDF_UTIL)/putchar_debug.c

directories:
	$(info $(shell mkdir -p $(BUILD_DIR)/lwip/)) \
	$(info $(shell mkdir -p $(BUILD_DIR)/lwip/core)) \
	$(info $(shell mkdir -p $(BUILD_DIR)/lwip/netif)) \
	$(info $(shell mkdir -p $(BUILD_DIR)/lwip/core/ipv4))
