#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
MICROKIT_SDK:=$(abspath ${MICROKIT_SDK})

ifeq ($(strip $(LIBGCC)),)
LIBGCC:=$(dir $(realpath $(shell aarch64-none-elf-gcc --print-file-name libgcc.a)))
endif

ifeq ($(strip $(LIBMATH)),)
LIBMATH:=$(dir $(realpath $(shell aarch64-none-elf-gcc --print-file-name libm.a)))
endif

MICROKIT_CONFIG ?= debug
BUILD_DIR ?= $(abspath build)
MICROKIT_BOARD := odroidc4
ETHERNET_CONFIG_INCLUDE ?= src/ethernet_config
CPU := cortex-a55

TOOLCHAIN := clang
CC := clang
LD := ld.lld
TARGET := aarch64-none-elf
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
DTC := dtc

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
LIONSOS ?= ../..
SDDF := $(LIONSOS)/sddf
LIBVMM := $(LIONSOS)/libvmm

VMM_IMAGE_DIR := src/vmm/images
LINUX := $(VMM_IMAGE_DIR)/linux
INITRD := $(VMM_IMAGE_DIR)/rootfs.cpio.gz
DTS := $(VMM_IMAGE_DIR)/linux.dts
DTB := $(BUILD_DIR)/linux.dtb

LWIP=$(SDDF)/network/ipstacks/lwip/src
LIBNFS=$(LIONSOS)/libnfs
NFS=$(LIONSOS)/fs/nfs
MUSL=$(LIONSOS)/musllibc

IMAGES := timer.elf vmm.elf ethernet.elf micropython.elf nfs.elf eth_copy.elf eth_virt_rx.elf eth_virt_tx.elf arp.elf uart_driver.elf serial_virt_rx.elf serial_virt_tx.elf i2c_virt.elf i2c_driver.elf
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
	-I$(SDDF)/include
LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld

IMAGE_FILE := $(BUILD_DIR)/kitty.img
REPORT_FILE := $(BUILD_DIR)/report.txt

TIMER_OBJS := timer.o sddf_util_printf.o sddf_putchar_debug.o
VMM_OBJS := vmm.o printf.o virq.o linux.o \
			guest.o psci.o smc.o fault.o \
			util.o vgic.o vgic_v2.o \
			package_guest_images.o tcb.o vcpu.o

TIMER_DRIVER := $(SDDF)/drivers/clock/meson

all: $(DIRECTORIES) $(BUILD_DIR) $(IMAGE_FILE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/libm/libm.a: $(LIBMATH)
	mkdir -p $(BUILD_DIR)/libm
	cp $(LIBMATH)/libm.a $(BUILD_DIR)/libm/libm.a

$(BUILD_DIR)/%.o: src/%.c
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: src/vmm/%.c
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/sddf_util_printf.o: $(SDDF)/util/printf.c $(SDDF)/include/sddf/util/printf.h
	$(CC) -c $(CFLAGS) $(SDDF)/util/printf.c -o $@

$(BUILD_DIR)/sddf_putchar_debug.o: $(SDDF)/util/putchar_debug.c
	$(CC) -c $(CFLAGS) $(SDDF)/util/putchar_debug.c -o $(BUILD_DIR)/sddf_putchar_debug.o

$(BUILD_DIR)/sddf_putchar_serial.o: $(SDDF)/util/putchar_serial.c
	$(CC) -c $(CFLAGS) $(SDDF)/util/putchar_serial.c -o $(BUILD_DIR)/sddf_putchar_serial.o

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

$(BUILD_DIR)/timer.elf: $(addprefix $(BUILD_DIR)/, $(TIMER_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/micropython.elf: FORCE mpy-cross $(BUILD_DIR)/sddf_util_cache.o $(BUILD_DIR)/libm/libm.a
	make -C src/micropython -j$(nproc) \
			MICROKIT_SDK=$(MICROKIT_SDK) \
			MICROKIT_BOARD=$(MICROKIT_BOARD) \
			MICROKIT_CONFIG=$(MICROKIT_CONFIG) \
			BUILD=$(abspath $(BUILD_DIR)) \
			LIBMATH=$(abspath $(BUILD_DIR)/libm) \
			ETHERNET_CONFIG_INCLUDE=$(abspath $(ETHERNET_CONFIG_INCLUDE))

ETHERNET=$(SDDF)/drivers/network/meson/
$(BUILD_DIR)/meson_ethernet.o: FORCE
	BUILD_DIR=$(abspath $(BUILD_DIR)) MICROKIT_INCLUDE=$(BOARD_DIR)/include ETHERNET_CONFIG_INCLUDE=$(abspath $(ETHERNET_CONFIG_INCLUDE)) make -C $(ETHERNET)

$(BUILD_DIR)/sddf_util_cache.o: $(SDDF)/util/cache.c
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/musllibc/lib/libc.a:
	make -C $(MUSL) \
		C_COMPILER=aarch64-none-elf-gcc \
		TOOLPREFIX=aarch64-none-elf- \
		CONFIG_ARCH_AARCH64=y \
		STAGE_DIR=$(abspath $(BUILD_DIR)/musllibc) \
		SOURCE_DIR=.

$(BUILD_DIR)/libnfs/lib/libnfs.a: $(BUILD_DIR)/musllibc/lib/libc.a
	MUSL=$(abspath $(BUILD_DIR)/musllibc) cmake -S $(LIONSOS)/libnfs -B $(BUILD_DIR)/libnfs
	cmake --build $(BUILD_DIR)/libnfs

$(BUILD_DIR)/nfs/nfs.a: FORCE $(BUILD_DIR)/musllibc/lib/libc.a
	make -C $(NFS) \
		BUILD_DIR=$(abspath $(BUILD_DIR)/nfs) \
		MICROKIT_INCLUDE=$(BOARD_DIR)/include \
		MUSLLIBC_INCLUDE=$(abspath $(BUILD_DIR)/musllibc/include) \
		LIBNFS_INCLUDE=$(abspath $(LIONSOS)/libnfs/include) \
		ETHERNET_CONFIG_INCLUDE=$(abspath $(ETHERNET_CONFIG_INCLUDE))

$(BUILD_DIR)/nfs.elf: $(BUILD_DIR)/nfs/nfs.a $(BUILD_DIR)/libnfs/lib/libnfs.a $(BUILD_DIR)/musllibc/lib/libc.a
	$(LD) \
		$(LDFLAGS) \
		$(BUILD_DIR)/nfs/nfs.a \
		-L$(BUILD_DIR)/libnfs/lib \
		-L$(BUILD_DIR)/musllibc/lib \
		-L$(LIBGCC) \
		-lgcc \
		-lc \
		$(LIBS) \
		-lnfs \
		-o $(BUILD_DIR)/nfs.elf

$(BUILD_DIR)/ethernet.elf: $(BUILD_DIR)/meson_ethernet.o $(BUILD_DIR)/sddf_util_printf.o $(BUILD_DIR)/sddf_putchar_debug.o
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

NETWORK_COMPONENTS := $(SDDF)/network/components
NETWORK_COMPONENTS_INCLUDE := \
	-I$(SDDF)/include \
	-I$(BOARD_DIR)/include \
	-I$(LWIP)/include \
	-I$(LWIP)/include/ipv4 \
	-Isrc/ethernet_config
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
	-MD \
	-MP

NETWORK_COMPONENTS_LDFLAGS := -L$(BOARD_DIR)/lib -L$(SDDF)/lib -L$(LIBGCC)
NETWORK_COMPONENTS_LIBS := -lmicrokit -Tmicrokit.ld -lc -lgcc $(BUILD_DIR)/sddf_util_cache.o $(BUILD_DIR)/sddf_util_printf.o

$(BUILD_DIR)/eth_copy.o: $(NETWORK_COMPONENTS)/copy.c
	aarch64-none-elf-gcc -c $(NETWORK_COMPONENT_CFLAGS) $< -o $@

$(BUILD_DIR)/eth_virt_rx.o: $(NETWORK_COMPONENTS)/virt_rx.c
	aarch64-none-elf-gcc -c $(NETWORK_COMPONENT_CFLAGS) $< -o $@

$(BUILD_DIR)/eth_virt_tx.o: $(NETWORK_COMPONENTS)/virt_tx.c
	aarch64-none-elf-gcc -c $(NETWORK_COMPONENT_CFLAGS) $< -o $@

$(BUILD_DIR)/arp.o: $(NETWORK_COMPONENTS)/arp.c
	aarch64-none-elf-gcc -c $(NETWORK_COMPONENT_CFLAGS) $< -o $@

$(BUILD_DIR)/eth_copy.elf: $(BUILD_DIR)/eth_copy.o $(BUILD_DIR)/sddf_putchar_debug.o
	aarch64-none-elf-ld $(NETWORK_COMPONENTS_LDFLAGS) $(BUILD_DIR)/eth_copy.o $(NETWORK_COMPONENTS_LIBS) $(BUILD_DIR)/sddf_putchar_debug.o -o $(BUILD_DIR)/eth_copy.elf

$(BUILD_DIR)/eth_virt_rx.elf: $(BUILD_DIR)/eth_virt_rx.o $(BUILD_DIR)/sddf_util_cache.o $(BUILD_DIR)/sddf_putchar_debug.o
	aarch64-none-elf-ld $(NETWORK_COMPONENTS_LDFLAGS) $(BUILD_DIR)/eth_virt_rx.o $(NETWORK_COMPONENTS_LIBS) $(BUILD_DIR)/sddf_putchar_debug.o -o $(BUILD_DIR)/eth_virt_rx.elf

$(BUILD_DIR)/eth_virt_tx.elf: $(BUILD_DIR)/eth_virt_tx.o $(BUILD_DIR)/sddf_util_cache.o $(BUILD_DIR)/sddf_putchar_debug.o
	aarch64-none-elf-ld $(NETWORK_COMPONENTS_LDFLAGS) $(BUILD_DIR)/eth_virt_tx.o $(NETWORK_COMPONENTS_LIBS) $(BUILD_DIR)/sddf_putchar_debug.o -o $(BUILD_DIR)/eth_virt_tx.elf

$(BUILD_DIR)/arp.elf: $(BUILD_DIR)/arp.o $(BUILD_DIR)/nfs/nfs.a $(BUILD_DIR)/sddf_putchar_serial.o
	aarch64-none-elf-ld $(NETWORK_COMPONENTS_LDFLAGS) $(BUILD_DIR)/arp.o $(BUILD_DIR)/nfs/lwip/core/inet_chksum.o $(BUILD_DIR)/nfs/lwip/core/def.o $(NETWORK_COMPONENTS_LIBS) $(BUILD_DIR)/sddf_putchar_serial.o -o $(BUILD_DIR)/arp.elf

# Compiling serial components
SERIAL_COMPONENTS := $(SDDF)/serial/components
UART_DRIVER := $(SDDF)/drivers/serial/meson
UART_DRIVER_OBJS := uart_driver.o sddf_putchar_debug.o sddf_util_printf.o
SERIAL_VIRT_RX_OBJS := serial_virt_rx.o
SERIAL_VIRT_TX_OBJS := serial_virt_tx.o

SERIAL_INCLUDES := -I$(SDDF)/util/include -I$(UART_DRIVER)/include
SERIAL_NUM_CLIENTS := -DSERIAL_NUM_CLIENTS=1

$(BUILD_DIR)/uart_driver.o: $(UART_DRIVER)/uart.c
	$(CC) -c $(SERIAL_INCLUDES) $(CFLAGS) $< -o $@

$(BUILD_DIR)/serial_virt_rx.o: $(SERIAL_COMPONENTS)/virt_rx.c
	$(CC) -c $(SERIAL_INCLUDES) $(CFLAGS) $(SERIAL_NUM_CLIENTS) $< -o $@

$(BUILD_DIR)/serial_virt_tx.o: $(SERIAL_COMPONENTS)/virt_tx.c
	$(CC) -c $(SERIAL_INCLUDES) $(CFLAGS) $(SERIAL_NUM_CLIENTS) $< -o $@

$(BUILD_DIR)/uart_driver.elf: $(addprefix $(BUILD_DIR)/, $(UART_DRIVER_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/serial_virt_rx.elf: $(addprefix $(BUILD_DIR)/, $(SERIAL_VIRT_RX_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/serial_virt_tx.elf: $(addprefix $(BUILD_DIR)/, $(SERIAL_VIRT_TX_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

# Compiling I2C components
I2C_QUEUE := $(SDDF)/i2c/queue
I2C_DRIVER := $(SDDF)/drivers/i2c/meson
I2C_VIRT := $(SDDF)/i2c/components
I2C_VIRT_OBJS := i2c_virt.o sddf_util_printf.o sddf_putchar_debug.o
I2C_DRIVER_OBJS := i2c_driver.o sddf_util_printf.o sddf_putchar_debug.o

$(BUILD_DIR)/i2c_driver.o: $(I2C_DRIVER)/i2c.c
	$(CC) -c -DI2C_BUS_NUM=2 $(CFLAGS) $< -o $@

$(BUILD_DIR)/i2c_virt.o: $(I2C_VIRT)/virt.c
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/i2c_virt.elf: $(addprefix $(BUILD_DIR)/, $(I2C_VIRT_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/i2c_driver.elf: $(addprefix $(BUILD_DIR)/, $(I2C_DRIVER_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

directories:
	$(shell mkdir -p $(BUILD_DIR)/lwip/src)
	$(shell mkdir -p $(BUILD_DIR)/lwip/src/core)
	$(shell mkdir -p $(BUILD_DIR)/lwip/src/netif)
	$(shell mkdir -p $(BUILD_DIR)/lwip/src/core/ipv4)
	$(shell mkdir -p $(BUILD_DIR)/nfs)

$(IMAGE_FILE) $(REPORT_FILE): $(addprefix $(BUILD_DIR)/, $(IMAGES)) kitty.system
	$(MICROKIT_TOOL) kitty.system --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

FORCE: ;

mpy-cross:
	make -C $(LIONSOS)/micropython/mpy-cross

.PHONY: mpy-cross
