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
SDDF := $(LIONSOS)/dep/sddf
LIBVMM := $(LIONSOS)/dep/libvmm

IMAGES := micropython.elf uart_driver.elf serial_virt_rx.elf serial_virt_tx.elf
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

all: $(DIRECTORIES) $(BUILD_DIR) $(IMAGE_FILE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/libm/libm.a: $(LIBMATH)
	mkdir -p $(BUILD_DIR)/libm
	cp $(LIBMATH)/libm.a $(BUILD_DIR)/libm/libm.a

$(BUILD_DIR)/sddf_util_printf.o: $(SDDF)/util/printf.c $(SDDF)/include/sddf/util/printf.h
	$(CC) -c $(CFLAGS) $(SDDF)/util/printf.c -o $@

$(BUILD_DIR)/sddf_putchar_debug.o: $(SDDF)/util/putchar_debug.c
	$(CC) -c $(CFLAGS) $(SDDF)/util/putchar_debug.c -o $(BUILD_DIR)/sddf_putchar_debug.o

$(BUILD_DIR)/sddf_putchar_serial.o: $(SDDF)/util/putchar_serial.c
	$(CC) -c $(CFLAGS) $(SDDF)/util/putchar_serial.c -o $(BUILD_DIR)/sddf_putchar_serial.o

$(BUILD_DIR)/sddf_util_cache.o: $(SDDF)/util/cache.c
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/micropython.elf: FORCE mpy-cross $(BUILD_DIR)/sddf_util_cache.o $(BUILD_DIR)/libm/libm.a
	make -C $(LIONSOS)/components/micropython -j$(nproc) \
			MICROKIT_SDK=$(MICROKIT_SDK) \
			MICROKIT_BOARD=$(MICROKIT_BOARD) \
			MICROKIT_CONFIG=$(MICROKIT_CONFIG) \
			BUILD=$(abspath $(BUILD_DIR)) \
			LIBMATH=$(abspath $(BUILD_DIR)/libm) \
			ETHERNET_CONFIG_INCLUDE=$(abspath $(ETHERNET_CONFIG_INCLUDE))

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

$(IMAGE_FILE) $(REPORT_FILE): $(addprefix $(BUILD_DIR)/, $(IMAGES)) kitty.system
	$(MICROKIT_TOOL) kitty.system --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

FORCE: ;

mpy-cross:
	make -C $(LIONSOS)/dep/micropython/mpy-cross

.PHONY: mpy-cross
