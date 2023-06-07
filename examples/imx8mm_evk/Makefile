#
# Copyright 2022, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

# @ivanv: Changes any of the params given e.g SEL4CP_CONFIG,
# doesn't actually cause a re-compile. We should fix this.
ifeq ($(strip $(BUILD_DIR)),)
$(error BUILD_DIR must be specified)
endif

ifeq ($(strip $(SEL4CP_SDK)),)
$(error SEL4CP_SDK must be specified)
endif

ifeq ($(strip $(SEL4CP_CONFIG)),)
$(error SEL4CP_CONFIG must be specified)
endif

ifeq ($(strip $(TOOLCHAIN)),)
	TOOLCHAIN := aarch64-none-elf
endif

CPU := cortex-a53

CC := $(TOOLCHAIN)-gcc
LD := $(TOOLCHAIN)-ld
AS := $(TOOLCHAIN)-as
SEL4CP_TOOL ?= $(SEL4CP_SDK)/bin/sel4cp

SEL4CP_BOARD = imx8mm_evk

SDDF = ../../sddf
VMM = ../../sel4cp_vmm

LWIP = $(SDDF)/network/ipstacks/lwip/src
BENCHMARK = ../../benchmark
ETHRINGBUFFER = $(SDDF)/network/libethsharedringbuffer
UTIL = $(SDDF)

LIBC = ../../libc

NETWORK_DIR = $(SDDF)/network
NETWORK_COMPONENTS = $(NETWORK_DIR)/components
CLOCK_DIR = $(SDDF)/clock/drivers/imx

BOARD_DIR := $(SEL4CP_SDK)/board/$(SEL4CP_BOARD)/$(SEL4CP_CONFIG)

IMAGES := ethernet.elf lwip.elf benchmark.elf idle.elf mux_rx.elf mux_tx.elf copy.elf
CFLAGS := -mcpu=$(CPU) -mstrict-align -ffreestanding -g3 -O3 -Wall -Wno-unused-function -DNO_ASSERT
LDFLAGS := -L$(BOARD_DIR)/lib -L$(LIBC)
LIBS := -lsel4cp -Tsel4cp.ld -lc

IMAGE_FILE = $(BUILD_DIR)/loader.img
REPORT_FILE = $(BUILD_DIR)/report.txt
SYSTEM_FILE = example.system

CFLAGS += -I$(BOARD_DIR)/include \
	-Iinclude	\
	-Iinclude/arch	\
	-I$(LWIP)/include \
	-I$(LWIP)/include/ipv4 \
	-isystem $(NETWORK_DIR) \
	-I$(BENCHMARK)/include \
	-isystem $(UTIL) \
	-isystem $(CLOCK_DIR) \
	-MD \
	-MP

# COREFILES, CORE4FILES: The minimum set of files needed for lwIP.
COREFILES=$(LWIP)/core/init.c \
	$(LWIP)/core/def.c \
	$(LWIP)/core/dns.c \
	$(LWIP)/core/inet_chksum.c \
	$(LWIP)/core/ip.c \
	$(LWIP)/core/mem.c \
	$(LWIP)/core/memp.c \
	$(LWIP)/core/netif.c \
	$(LWIP)/core/pbuf.c \
	$(LWIP)/core/raw.c \
	$(LWIP)/core/stats.c \
	$(LWIP)/core/sys.c \
	$(LWIP)/core/altcp.c \
	$(LWIP)/core/altcp_alloc.c \
	$(LWIP)/core/altcp_tcp.c \
	$(LWIP)/core/tcp.c \
	$(LWIP)/core/tcp_in.c \
	$(LWIP)/core/tcp_out.c \
	$(LWIP)/core/timeouts.c \
	$(LWIP)/core/udp.c

CORE4FILES=$(LWIP)/core/ipv4/autoip.c \
	$(LWIP)/core/ipv4/dhcp.c \
	$(LWIP)/core/ipv4/etharp.c \
	$(LWIP)/core/ipv4/icmp.c \
	$(LWIP)/core/ipv4/igmp.c \
	$(LWIP)/core/ipv4/ip4_frag.c \
	$(LWIP)/core/ipv4/ip4.c \
	$(LWIP)/core/ipv4/ip4_addr.c

# NETIFFILES: Files implementing various generic network interface functions
NETIFFILES=$(LWIP)/netif/ethernet.c

SHARED_RING_BUFFER = $(ETHRINGBUFFER)/shared_ringbuffer.o

# LWIPFILES: All the above.
LWIPFILES=$(NETWORK_COMPONENTS)/lwip.c $(COREFILES) $(CORE4FILES) $(NETIFFILES)
LWIP_OBJS := $(LWIPFILES:.c=.o) $(NETWORK_COMPONENTS)/lwip.o \
			 $(SHARED_RING_BUFFER) \
			 $(NETWORK_COMPONENTS)/utilization_socket.o \
			 $(NETWORK_COMPONENTS)/udp_echo_socket.o \
			 $(CLOCK_DIR)/timer.o

ETH_OBJS := $(SDDF)/network/drivers/imx/ethernet.o $(SHARED_RING_BUFFER)
MUX_RX_OBJS := $(SDDF)/network/muxs/rx.o $(SHARED_RING_BUFFER)
MUX_TX_OBJS := $(SDDF)/network/muxs/tx.o $(SHARED_RING_BUFFER)
COPY_OBJS := $(NETWORK_COMPONENTS)/copy.o $(SHARED_RING_BUFFER)
BENCH_OBJS := $(BENCHMARK)/benchmark.o
IDLE_OBJS := $(BENCHMARK)/idle.o

OBJS := $(sort $(addprefix $(BUILD_DIR)/, $(ETH_OBJS) $(MUX_RX_OBJS) $(MUX_TX_OBJS) \
	$(COPY_OBJS) $(BENCH_OBJS) $(IDLE_OBJS) \
	$(LWIP_OBJS)))
DEPS := $(OBJS:.o=.d)

all: $(IMAGE_FILE)
-include $(DEPS)

$(BUILD_DIR)/%.d $(BUILD_DIR)/%.o: %.c
	mkdir -p `dirname $(BUILD_DIR)/$*.o`
	$(CC) -c $(CFLAGS) $< -o $(BUILD_DIR)/$*.o

$(BUILD_DIR)/%.o: %.s Makefile
	$(AS) -g3 -mcpu=$(CPU) $< -o $@

$(BUILD_DIR)/ethernet.elf: $(addprefix $(BUILD_DIR)/, $(ETH_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/lwip.elf: $(addprefix $(BUILD_DIR)/, $(LWIP_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/mux_rx.elf: $(addprefix $(BUILD_DIR)/, $(MUX_RX_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/mux_tx.elf: $(addprefix $(BUILD_DIR)/, $(MUX_TX_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/copy.elf: $(addprefix $(BUILD_DIR)/, $(COPY_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/benchmark.elf: $(addprefix $(BUILD_DIR)/, $(BENCH_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/idle.elf: $(addprefix $(BUILD_DIR)/, $(IDLE_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/serial.elf: $(addprefix $(BUILD_DIR)/, $(SERIAL_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(IMAGE_FILE) $(REPORT_FILE): $(addprefix $(BUILD_DIR)/, $(IMAGES)) $(SYSTEM_FILE)
	$(SEL4CP_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(SEL4CP_BOARD) --config $(SEL4CP_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

.PHONY: all compile clean

%.o:
	$(CC) $(CFLAGS) -c $(@:.o=.c) -o $@

clean:
	rm -f *.o *.elf .depend*
	find . -name \*.o |xargs --no-run-if-empty rm

