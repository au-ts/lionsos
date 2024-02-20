#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif

ifeq ($(strip $(LIBGCC)),)
LIBGCC := $(shell dirname $$(aarch64-none-elf-gcc --print-file-name libgcc.a))
endif

MICROKIT_CONFIG ?= debug
BUILD_DIR ?= $(abspath build)
MICROKIT_BOARD := odroidc4
CPU := cortex-a55

# '?=' means the variable is regenerated every time the macros is invoked.
# Get one value once, and use that (':=')
# If we've buit part of the system with a MAC address, use that and
# don't geneate a new one
-include ${BUILD_DIR}/mac_address
XMAC_BASE_ADDRESS ?= $(shell < /dev/urandom tr -d -c '[:digit:]A-F' | fold -w12 | sed -E -n -e '/^.[26AE]/s/^/0x/' -e '/0x/{s/$$/ULL/;p;q}')
export MAC_BASE_ADDRESS := ${XMAC_BASE_ADDRESS}

TOOLCHAIN := clang
CC := clang
LD := ld.lld
TARGET := aarch64-none-elf
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
DTC := dtc

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
SDDF := ../../sddf
LIBVMM := ../../vmm

VMM_IMAGE_DIR := src/vmm/images
LINUX := $(VMM_IMAGE_DIR)/linux
INITRD := $(VMM_IMAGE_DIR)/rootfs.cpio.gz
DTS := $(VMM_IMAGE_DIR)/linux.dts
DTB := $(BUILD_DIR)/linux.dtb

LWIP=$(SDDF)/network/ipstacks/lwip/src
LIBNFS=../../libnfs
NFS=../../fs/nfs
MUSL=../../musllibc

IMAGES := main.elf timer.elf vmm.elf ethernet.elf micropython.elf nfs.elf copy.elf mux_rx.elf mux_tx.elf arp.elf uart_driver.elf serial_mux_rx.elf serial_mux_tx.elf
CFLAGS := \
	-mtune=$(CPU) \
	-mstrict-align \
	-ffreestanding \
	-g \
	-O0 \
	-Wall \
	-Wno-unused-function \
	-I$(BOARD_DIR)/include \
	-target $(TARGET) \
	-I$(LIBVMM)/src/arch/aarch64 \
	-I$(LIBVMM)/src \
	-I$(LIBVMM)/src/util \
	-DBOARD_$(MICROKIT_BOARD) \
	-I$(SDDF)/include \
	-DMAC_BASE_ADDRESS=$(MAC_BASE_ADDRESS)

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld

IMAGE_FILE := $(BUILD_DIR)/kitty.img
REPORT_FILE := $(BUILD_DIR)/report.txt

MAIN_OBJS := main.o
TIMER_OBJS := timer.o
VMM_OBJS := vmm.o printf.o virq.o linux.o \
			guest.o psci.o smc.o fault.o \
			util.o vgic.o vgic_v2.o \
			package_guest_images.o tcb.o vcpu.o

TIMER_DRIVER := $(SDDF)/drivers/clock/meson

PACKAGE_PYTHON_SCRIPTS := src/package_python_scripts.S

all: $(DIRECTORIES) $(BUILD_DIR) $(IMAGE_FILE) 

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: src/vmm/%.c
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/mac_address: ${BUILD_DIR}
	echo "XMAC_BASE_ADDRESS=${MAC_BASE_ADDRESS}" > $@

$(BUILD_DIR)/timer.o: $(TIMER_DRIVER)/timer.c
	$(CC) -c $(CFLAGS) $< -o $@

$(DTB): $(DTS)
	$(DTC) -q -I dts -O dtb $< > $@

$(BUILD_DIR)/package_guest_images.o: $(LIBVMM)/tools/package_guest_images.S $(VMM_IMAGE_DIR) $(LINUX) $(INITRD) $(DTB)
	$(CC) -c -g3 -x assembler-with-cpp \
					-DGUEST_KERNEL_IMAGE_PATH=\"$(LINUX)\" \
					-DGUEST_DTB_IMAGE_PATH=\"$(DTB)\" \
					-DGUEST_INITRD_IMAGE_PATH=\"$(INITRD)\" \
					-target $(TARGET) \
					$< -o $@

$(BUILD_DIR)/%.o: $(LIBVMM)/src/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(LIBVMM)/src/util/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(LIBVMM)/src/arch/aarch64/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(LIBVMM)/src/arch/aarch64/vgic/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/vmm.elf: $(addprefix $(BUILD_DIR)/, $(VMM_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/main.elf: $(addprefix $(BUILD_DIR)/, $(MAIN_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/timer.elf: $(addprefix $(BUILD_DIR)/, $(TIMER_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/package_python_scripts.o: $(PACKAGE_PYTHON_SCRIPTS) src/kitty.py
	$(CC) -c -g3 -x assembler-with-cpp \
				-DKITTY_PYTHON_SCRIPT_PATH=\"src/kitty.py\" \
				-target $(TARGET) \
				$< -o $@

$(BUILD_DIR)/micropython.elf: FORCE $(BUILD_DIR)/package_python_scripts.o $(BUILD_DIR)/sddf_timer_client.o $(BUILD_DIR)/sddf_fs_protocol.o $(BUILD_DIR)/sddf_serial_sharedringbuffer.o
	make -C src/micropython -j$(nproc) \
			MICROKIT_SDK=$(MICROKIT_SDK) \
			MICROKIT_BOARD=$(MICROKIT_BOARD) \
			MICROKIT_CONFIG=$(MICROKIT_CONFIG) \
			BUILD=$(abspath $(BUILD_DIR)) \
			PACKAGE_PYTHON_SCRIPTS=package_python_scripts.o

ETHERNET=$(SDDF)/drivers/network/meson/
$(BUILD_DIR)/meson_ethernet.o: FORCE
	BUILD_DIR=$(abspath $(BUILD_DIR)) MICROKIT_INCLUDE=$(BOARD_DIR)/include make -C $(ETHERNET)

LIBSHAREDRINGBUFFER=$(SDDF)/network/libethsharedringbuffer/
$(BUILD_DIR)/sddf_network_sharedringbuffer.o: FORCE
	BUILD_DIR=$(abspath $(BUILD_DIR)) MICROKIT_INCLUDE=$(BOARD_DIR)/include make -C $(LIBSHAREDRINGBUFFER)

$(BUILD_DIR)/sddf_timer_client.o: FORCE
	BUILD_DIR=$(abspath $(BUILD_DIR)) MICROKIT_INCLUDE=$(BOARD_DIR)/include TIMER_CHANNEL=9 make -C $(SDDF)/timer/client

SDDF_FS_PROTOCOL=../../fs/protocol/
$(BUILD_DIR)/sddf_fs_protocol.o: FORCE
	BUILD_DIR=$(abspath $(BUILD_DIR)) make -C $(SDDF_FS_PROTOCOL)

$(BUILD_DIR)/musllibc/lib/libc.a:
	make -C $(MUSL) \
		C_COMPILER=aarch64-none-elf-gcc \
		TOOLPREFIX=aarch64-none-elf- \
		CONFIG_ARCH_AARCH64=y \
		STAGE_DIR=$(abspath $(BUILD_DIR)/musllibc) \
		SOURCE_DIR=.

$(BUILD_DIR)/libnfs/lib/libnfs.a: $(BUILD_DIR)/musllibc/lib/libc.a
	MUSL=$(abspath $(BUILD_DIR)/musllibc) cmake -S ../../libnfs -B $(BUILD_DIR)/libnfs
	cmake --build $(BUILD_DIR)/libnfs

$(BUILD_DIR)/nfs/nfs.a: FORCE $(BUILD_DIR)/musllibc/lib/libc.a
	make -C $(NFS) \
		BUILD_DIR=$(abspath $(BUILD_DIR)/nfs) \
		MICROKIT_INCLUDE=$(BOARD_DIR)/include \
		MUSLLIBC_INCLUDE=$(abspath $(BUILD_DIR)/musllibc/include) \
		LIBNFS_INCLUDE=$(abspath ../..//libnfs/include)

$(BUILD_DIR)/nfs.elf: $(BUILD_DIR)/nfs/nfs.a $(BUILD_DIR)/sddf_network_sharedringbuffer.o $(BUILD_DIR)/sddf_timer_client.o $(BUILD_DIR)/sddf_fs_protocol.o $(BUILD_DIR)/libnfs/lib/libnfs.a $(BUILD_DIR)/musllibc/lib/libc.a
	$(LD) \
		$(LDFLAGS) \
		$(BUILD_DIR)/nfs/nfs.a \
		$(BUILD_DIR)/sddf_network_sharedringbuffer.o \
		$(BUILD_DIR)/sddf_timer_client.o \
		$(BUILD_DIR)/sddf_fs_protocol.o \
		-L$(BUILD_DIR)/libnfs/lib \
		-L$(BUILD_DIR)/musllibc/lib \
		-L$(LIBGCC) \
		-lgcc \
		-lc \
		$(LIBS) \
		-lnfs \
		-o $(BUILD_DIR)/nfs.elf

$(BUILD_DIR)/ethernet.elf: $(BUILD_DIR)/meson_ethernet.o $(BUILD_DIR)/sddf_network_sharedringbuffer.o
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

NETWORK_COMPONENTS := $(SDDF)/network/components
NETWORK_COMPONENTS_INCLUDE := \
	-I$(SDDF)/include \
	-I$(SDDF)/util/include \
	-I$(BOARD_DIR)/include \
	-I$(LWIP)/include \
	-I$(LWIP)/include/ipv4
NETWORK_COMPONENT_CFLAGS := \
	$(NETWORK_COMPONENTS_INCLUDE) \
	-mstrict-align \
	-ffreestanding \
	-g \
	-O0 \
	-Wall \
	-Wno-unused-function \
	-DNO_ASSERT \
	-DBOARD_$(MICROKIT_BOARD) \
	-DMAC_BASE_ADDRESS=$(MAC_BASE_ADDRESS) \
	-MD \
	-MP

NETWORK_COMPONENTS_LDFLAGS := -L$(BOARD_DIR)/lib -L$(SDDF)/lib -L$(LIBGCC)
NETWORK_COMPONENTS_LIBS := -lmicrokit -Tmicrokit.ld -lc -lgcc

$(BUILD_DIR)/copy.o: $(NETWORK_COMPONENTS)/copy.c
	aarch64-none-elf-gcc -c $(NETWORK_COMPONENT_CFLAGS) $< -o $@

$(BUILD_DIR)/mux_rx.o: $(NETWORK_COMPONENTS)/mux_rx.c
	aarch64-none-elf-gcc -c $(NETWORK_COMPONENT_CFLAGS) $< -o $@

$(BUILD_DIR)/mux_tx.o: $(NETWORK_COMPONENTS)/mux_tx.c
	aarch64-none-elf-gcc -c $(NETWORK_COMPONENT_CFLAGS) $< -o $@

$(BUILD_DIR)/arp.o: $(NETWORK_COMPONENTS)/arp.c
	aarch64-none-elf-gcc -c $(NETWORK_COMPONENT_CFLAGS) $< -o $@

$(BUILD_DIR)/copy.elf: $(BUILD_DIR)/copy.o $(BUILD_DIR)/sddf_network_sharedringbuffer.o
	aarch64-none-elf-ld $(NETWORK_COMPONENTS_LDFLAGS) $(BUILD_DIR)/sddf_network_sharedringbuffer.o $(BUILD_DIR)/copy.o $(NETWORK_COMPONENTS_LIBS) -o $(BUILD_DIR)/copy.elf

$(BUILD_DIR)/mux_rx.elf: $(BUILD_DIR)/mux_rx.o $(BUILD_DIR)/sddf_network_sharedringbuffer.o
	aarch64-none-elf-ld $(NETWORK_COMPONENTS_LDFLAGS) $(BUILD_DIR)/sddf_network_sharedringbuffer.o $(BUILD_DIR)/mux_rx.o $(NETWORK_COMPONENTS_LIBS) -o $(BUILD_DIR)/mux_rx.elf

$(BUILD_DIR)/mux_tx.elf: $(BUILD_DIR)/mux_tx.o $(BUILD_DIR)/sddf_network_sharedringbuffer.o
	aarch64-none-elf-ld $(NETWORK_COMPONENTS_LDFLAGS) $(BUILD_DIR)/sddf_network_sharedringbuffer.o $(BUILD_DIR)/mux_tx.o $(NETWORK_COMPONENTS_LIBS) -o $(BUILD_DIR)/mux_tx.elf

$(BUILD_DIR)/arp.elf: $(BUILD_DIR)/arp.o $(BUILD_DIR)/sddf_network_sharedringbuffer.o $(BUILD_DIR)/nfs/nfs.a
	aarch64-none-elf-ld $(NETWORK_COMPONENTS_LDFLAGS) $(BUILD_DIR)/sddf_network_sharedringbuffer.o $(BUILD_DIR)/arp.o  $(BUILD_DIR)/nfs/lwip/core/inet_chksum.o $(BUILD_DIR)/nfs/lwip/core/def.o $(NETWORK_COMPONENTS_LIBS) -o $(BUILD_DIR)/arp.elf

# Compiling serial components
LIB_SERIAL_SHARED_RINGBUFFER := $(SDDF)/serial/libserialsharedringbuffer/
SERIAL_COMPONENTS := $(SDDF)/serial/components
UART_DRIVER := $(SDDF)/drivers/serial/meson
UART_DRIVER_OBJS := uart_driver.o sddf_serial_sharedringbuffer.o
SERIAL_MUX_RX_OBJS := serial_mux_rx.o sddf_serial_sharedringbuffer.o
SERIAL_MUX_TX_OBJS := serial_mux_tx.o sddf_serial_sharedringbuffer.o

SERIAL_INCLUDES := -I$(SDDF)/util/include -I$(UART_DRIVER)/include
SERIAL_NUM_CLIENTS := -DSERIAL_NUM_CLIENTS=1

$(BUILD_DIR)/sddf_serial_sharedringbuffer.o: FORCE
	BUILD_DIR=$(abspath $(BUILD_DIR)) MICROKIT_INCLUDE=$(BOARD_DIR)/include make -C $(LIB_SERIAL_SHARED_RINGBUFFER)

$(BUILD_DIR)/uart_driver.o: $(UART_DRIVER)/uart.c
	$(CC) -c $(SERIAL_INCLUDES) $(CFLAGS) $< -o $@

$(BUILD_DIR)/serial_mux_rx.o: $(SERIAL_COMPONENTS)/mux_rx.c
	$(CC) -c $(SERIAL_INCLUDES) $(CFLAGS) $(SERIAL_NUM_CLIENTS) $< -o $@

$(BUILD_DIR)/serial_mux_tx.o: $(SERIAL_COMPONENTS)/mux_tx.c
	$(CC) -c $(SERIAL_INCLUDES) $(CFLAGS) $(SERIAL_NUM_CLIENTS) $< -o $@

$(BUILD_DIR)/uart_driver.elf: $(addprefix $(BUILD_DIR)/, $(UART_DRIVER_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/serial_mux_rx.elf: $(addprefix $(BUILD_DIR)/, $(SERIAL_MUX_RX_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/serial_mux_tx.elf: $(addprefix $(BUILD_DIR)/, $(SERIAL_MUX_TX_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

directories:
	$(info $(shell mkdir -p $(BUILD_DIR)/lwip/src))	\
	$(info $(shell mkdir -p $(BUILD_DIR)/lwip/src/core)) 	\
	$(info $(shell mkdir -p $(BUILD_DIR)/lwip/src/netif))	\
	$(info $(shell mkdir -p $(BUILD_DIR)/lwip/src/core/ipv4)) \
	$(info $(shell mkdir -p $(BUILD_DIR)/nfs))

$(IMAGE_FILE) $(REPORT_FILE): $(addprefix $(BUILD_DIR)/, $(IMAGES)) kitty.system
	$(MICROKIT_TOOL) kitty.system --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

FORCE: ;
