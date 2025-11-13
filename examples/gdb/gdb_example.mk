#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#


SDDF=$(LIONSOS)/dep/sddf
LIBGDB_DIR=$(LIONSOS)/dep/libgdb
LIBVSPACE_DIR=$(LIBGDB_DIR)/libvspace

METAPROGRAM := $(TOP)/meta.py

MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
DEBUGGER:=${TOP}/apps/debugger
LWIP:=$(SDDF)/network/ipstacks/lwip/src
BENCHMARK:=$(SDDF)/benchmark
UTIL:=$(SDDF)/util
SERIAL_COMPONENTS := $(SDDF)/serial/components
NETWORK_COMPONENTS:=$(SDDF)/network/components
DEBUGGER_INCLUDE:=$(TOP)/net_debugger/include

PYTHON ?= python3

TOOLCHAIN ?= clang
BOARD := $(MICROKIT_BOARD)
BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
IMAGE_FILE := gdb.img
REPORT_FILE := report.txt
SYSTEM_FILE := sddf_net_example.system
METAPROGRAM := $(TOP)/meta.py

# The base images for this example
IMAGES := ping.elf pong.elf debugger.elf serial_driver.elf \
		serial_virt_tx.elf eth_driver.elf network_virt_rx.elf \
		network_virt_tx.elf network_copy.elf timer_driver.elf

SUPPORTED_BOARDS:= \
	maaxboard \
	odroidc4 \
	qemu_virt_aarch64

all: $(IMAGE_FILE)

include ${SDDF}/tools/make/board/common.mk

ETHERNET_DRIVER:=$(SDDF)/drivers/network/$(NET_DRIV_DIR)
SERIAL_DRIVER := $(SDDF)/drivers/serial/$(UART_DRIV_DIR)
TIMER_DRIVER:=$(SDDF)/drivers/timer/$(TIMER_DRIV_DIR)

vpath %.c ${SDDF} ${DEBUGGER}

include $(LIONSOS)/lib/libc/libc.mk

CFLAGS += \
	-DMICROKIT \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(SDDF)/libco \
	-I$(LIBGDB_DIR)/include \
	-I$(LIBGDB_DIR)/arch_include \
	-I$(LIONS_LIBC)/include \
	-I$(LIBVSPACE_DIR) \
	-I${DEBUGGER_INCLUDE}/lwip \
	-I$(LWIP)/include \
	-I$(LWIP)/include/ipv4 \

LDFLAGS := -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib
LIBS :=  -lmicrokit -Tmicrokit.ld -lc libsddf_util_debug.a libvspace.a

$(IMAGES): $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a libvspace.a ${CHECK_FLAGS_BOARD_MD5}

ping.o: $(TOP)/ping.c libsddf_util_debug.a
	$(CC) -c $(CFLAGS) $< -o $@

pong.o: $(TOP)/pong.c libsddf_util_debug.a
	$(CC) -c $(CFLAGS) $< -o $@

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	PYTHONPATH=${SDDF}/tools/meta:$$PYTHONPATH $(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE)
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .device_resources=ethernet_driver_device_resources.data eth_driver.elf
	$(OBJCOPY) --update-section .net_driver_config=net_driver.data eth_driver.elf
	$(OBJCOPY) --update-section .net_virt_rx_config=net_virt_rx.data network_virt_rx.elf
	$(OBJCOPY) --update-section .net_virt_tx_config=net_virt_tx.data network_virt_tx.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_debugger_net_copier.data network_copy.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_debugger.data debugger.elf
	$(OBJCOPY) --update-section .net_client_config=net_client_debugger.data debugger.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_debugger.data debugger.elf
	$(OBJCOPY) --update-section .lib_sddf_lwip_config=lib_sddf_lwip_config_debugger.data debugger.elf
	touch $@


${IMAGE_FILE} $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include

include ${SDDF}/util/util.mk
include ${SERIAL_DRIVER}/serial_driver.mk
include ${SERIAL_COMPONENTS}/serial_components.mk
include $(LIBGDB_DIR)/libgdb.mk
include $(LIBVSPACE_DIR)/libvspace.mk
include $(TOP)/net_debugger/debugger.mk
include ${SDDF}/network/components/network_components.mk
include ${SDDF}/network/lib_sddf_lwip/lib_sddf_lwip.mk
include ${SDDF}/libco/libco.mk
include ${TIMER_DRIVER}/timer_driver.mk
include $(TOP)/net_debugger/debugger.mk
include ${ETHERNET_DRIVER}/eth_driver.mk

qemu: $(IMAGE_FILE)
	$(QEMU) -machine virt,virtualization=on \
			-cpu cortex-a53 \
			-serial mon:stdio \
			-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
			-m size=2G \
			-nographic \
			-device virtio-net-device,netdev=netdev0 \
			-netdev user,id=netdev0,hostfwd=tcp::1234-:1234 \
			-global virtio-mmio.force-legacy=false \
			-d guest_errors

${SDDF}/tools/make/board/common.mk ${SDDF_MAKEFILES} ${LIONSOS}/dep/sddf/include &:
	cd $(LIONSOS); git submodule update --init dep/sddf