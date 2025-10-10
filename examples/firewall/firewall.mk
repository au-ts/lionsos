# Makefile for firewall.
#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This makefile will be copied into the Build directory and used from there.
#
BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
ARCH := $(shell grep 'CONFIG_SEL4_ARCH  ' $(BOARD_DIR)/include/kernel/gen_config.h | cut -d' ' -f4)
SDDF := $(LIONSOS)/dep/sddf
LWIP := $(SDDF)/network/ipstacks/lwip/src
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco

ifeq ($(strip $(MICROKIT_BOARD)), imx8mp_iotgate)
	ETH_DRIV_DIR0 := dwmac-5.10a
	ETH_DRIV_DIR1 := imx
	SERIAL_DRIV_DIR := imx
	TIMER_DRV_DIR := imx
	CPU := cortex-a53
else ifeq ($(strip $(MICROKIT_BOARD)), qemu_virt_aarch64)
	ETH_DRIV_DIR0 := virtio
	ETH_DRIV_DIR1 := virtio
	SERIAL_DRIV_DIR := arm
	TIMER_DRV_DIR := arm
	CPU := cortex-a53
	QEMU := qemu-system-aarch64
else
$(error Unsupported MICROKIT_BOARD given)
endif

ETH_DRIV0 := eth_driver_${ETH_DRIV_DIR0}.elf
ETH_DRIV1 := eth_driver_${ETH_DRIV_DIR1}.elf

TOOLCHAIN := clang
CC := clang
LD := ld.lld
AR := llvm-ar
RANLIB := llvm-ranlib
OBJCOPY := llvm-objcopy
OBJDUMP := llvm-objdump
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
PYTHON ?= python3
DTC := dtc

ifeq ($(ARCH),aarch64)
	CFLAGS_ARCH := -mcpu=$(CPU)
	TARGET := aarch64-none-elf
else ifeq ($(ARCH),riscv64)
	CFLAGS_ARCH := -march=rv64imafdc
	TARGET := riscv64-none-elf
else
$(error Unsupported ARCH given)
endif

ifeq ($(strip $(TOOLCHAIN)), clang)
	CFLAGS_ARCH += -target $(TARGET)
endif

MICRODOT := $(LIONSOS)/dep/microdot/src
FIREWALL_NET_COMPONENTS := $(FIREWALL_SRC_DIR)/net_components
FIREWALL_FILTERS := $(FIREWALL_SRC_DIR)/filters
FIREWALL_ICMP := $(FIREWALL_SRC_DIR)/icmp
FIREWALL_ROUTING := $(FIREWALL_SRC_DIR)/routing
FIREWALL_ARP := $(FIREWALL_SRC_DIR)/arp

METAPROGRAM := $(FIREWALL_SRC_DIR)/meta.py
DTS := $(SDDF)/dts/$(MICROKIT_BOARD).dts
DTB := $(MICROKIT_BOARD).dtb

SDFGEN_HELPER := $(FIREWALL_SRC_DIR)/sdfgen_helper.py
# Macros needed by sdfgen helper to calculate config struct sizes
SDFGEN_UNKOWN_MACROS := ETH_HWADDR_LEN=6 SDDF_NET_MAX_CLIENTS=64
# Headers containing config structs and dependencies
FIREWALL_CONFIG_HEADERS := $(SDDF)/include/sddf/resources/common.h \
							$(SDDF)/include/sddf/resources/device.h \
							$(LIONSOS)/include/lions/firewall/config.h

IMAGES := arp_requester.elf arp_responder.elf routing.elf micropython.elf \
		  firewall_network_virt_rx.elf firewall_network_virt_tx.elf \
		  timer_driver.elf serial_driver.elf serial_virt_tx.elf \
		  icmp_filter.elf udp_filter.elf tcp_filter.elf icmp_module.elf \
		  $(ETH_DRIV0) $(ETH_DRIV1) eth_driver0.elf eth_driver1.elf

DEPS := $(IMAGES:.elf=.d)

SYSTEM_FILE := firewall.system

CFLAGS := \
	-mtune=$(CPU) \
	-mstrict-align \
	-ffreestanding \
	-O2 \
	-g3 \
	-MD \
	-MP \
	-Wall \
	-Wno-unused-function \
	-Wno-bitwise-op-parentheses \
	-Wno-shift-op-parentheses \
	-I$(BOARD_DIR)/include \
	$(CFLAGS_ARCH) \
	-DBOARD_$(MICROKIT_BOARD) \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit

include $(LIONSOS)/lib/libc/libc.mk

LDFLAGS := -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib
LIBS := -lmicrokit -Tmicrokit.ld -lc libsddf_util_debug.a

IMAGE_FILE := firewall.img
REPORT_FILE := report.txt

all: $(IMAGE_FILE)
$(IMAGES): $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a

CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- $(CFLAGS) $(BOARD) $(MICROKIT_CONFIG) | shasum | sed 's/ *-//')

$(CHECK_FLAGS_BOARD_MD5):
	-rm -f .board_cflags-*
	touch $@

vpath %.c $(SDDF) $(FIREWALL_SRC_DIR) $(FIREWALL_NET_COMPONENTS) $(FIREWALL_FILTERS) $(FIREWALL_ICMP) $(FIREWALL_ROUTING) $(FIREWALL_ARP)

MICROPYTHON_LIBMATH := $(LIBMATH)
MICROPYTHON_EXEC_MODULE := ui_server.py
MICROPYTHON_FROZEN_MANIFEST := manifest.py
MICROPYTHON_USER_C_MODULES := modfirewall.c

$(MICROPYTHON_FROZEN_MANIFEST): $(MICROPYTHON_EXEC_MODULE)
$(MICROPYTHON_EXEC_MODULE): $(MICRODOT)

include $(LIONSOS)/components/micropython/micropython.mk

%.py: $(FIREWALL_SRC_DIR)/%.py
	cp $< $@

%.o: %.c | $(LIONS_LIBC)/include
	$(CC) $(CFLAGS) -c -o $@ $<

%.elf: %.o
	$(LD) $(LDFLAGS) $< $(LIBS) -o $@

eth_driver0.elf: $(ETH_DRIV0)
	cp $< $@

eth_driver1.elf: $(ETH_DRIV1)
	cp $< $@

# Components that print to serial require libsddf_util.a
arp_requester.elf: arp_requester.o libsddf_util.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

arp_responder.elf: arp_responder.o libsddf_util.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

routing.elf: routing.o libsddf_util.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include

SDDF_MAKEFILES := $(SDDF)/util/util.mk \
		  $(SDDF)/drivers/network/$(ETH_DRIV_DIR0)/eth_driver.mk \
		  $(SDDF)/drivers/network/$(ETH_DRIV_DIR1)/eth_driver.mk \
		  $(SDDF)/drivers/serial/$(SERIAL_DRIV_DIR)/serial_driver.mk \
		  $(SDDF)/drivers/timer/$(TIMER_DRV_DIR)/timer_driver.mk \
		  $(SDDF)/network/components/network_components.mk \
		  ${SDDF}/network/lib_sddf_lwip/lib_sddf_lwip.mk \
		  $(SDDF)/serial/components/serial_components.mk

include $(SDDF_MAKEFILES)
include $(FIREWALL_NET_COMPONENTS)/firewall_network_components.mk

LIBMICROKITCO_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIBMICROKITCO_PATH)/libmicrokitco.mk

$(DTB): $(DTS)
	$(DTC) -q -I dts -O dtb $(DTS) > $(DTB)

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB) $(CHECK_FLAGS_BOARD_MD5)
	$(PYTHON) $(SDFGEN_HELPER) --macros "$(SDFGEN_UNKOWN_MACROS)" --configs "$(FIREWALL_CONFIG_HEADERS)" --output $(BUILD_DIR)/config_structs.py
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE) --objcopy $(OBJCOPY) --objdump $(OBJDUMP)
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf

# Components receiving from or transmitting out net0
	$(OBJCOPY) --update-section .device_resources=net_data0/ethernet_driver0_device_resources.data eth_driver0.elf
	$(OBJCOPY) --update-section .net_driver_config=net_data0/net_driver.data eth_driver0.elf

	$(OBJCOPY) --update-section .net_virt_rx_config=net_data0/net_virt_rx.data firewall_network_virt_rx0.elf
	$(OBJCOPY) --update-section .net_virt_tx_config=net_data0/net_virt_tx.data firewall_network_virt_tx0.elf

# arp_requester1 receives requests from net1 router but transmits out net0
	$(OBJCOPY) --update-section .net_client_config=net_data0/net_client_arp_requester1.data arp_requester1.elf
	$(OBJCOPY) --update-section .net_client_config=net_data0/net_client_arp_responder0.data arp_responder0.elf
	$(OBJCOPY) --update-section .net_client_config=net_data0/net_client_icmp_filter0.data icmp_filter0.elf
	$(OBJCOPY) --update-section .net_client_config=net_data0/net_client_udp_filter0.data udp_filter0.elf
	$(OBJCOPY) --update-section .net_client_config=net_data0/net_client_tcp_filter0.data tcp_filter0.elf
	$(OBJCOPY) --update-section .ext_net_client_config=net_data0/net_client_icmp_module.data icmp_module.elf

	$(OBJCOPY) --update-section .serial_client_config=serial_client_arp_responder0.data arp_responder0.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_arp_requester0.data arp_requester0.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_routing0.data routing0.elf

	$(OBJCOPY) --update-section .timer_client_config=timer_client_arp_requester0.data arp_requester0.elf

# Components receiving from or transmitting out net1
	$(OBJCOPY) --update-section .device_resources=net_data1/ethernet_driver1_device_resources.data eth_driver1.elf
	$(OBJCOPY) --update-section .net_driver_config=net_data1/net_driver.data eth_driver1.elf

	$(OBJCOPY) --update-section .net_virt_rx_config=net_data1/net_virt_rx.data firewall_network_virt_rx1.elf
	$(OBJCOPY) --update-section .net_virt_tx_config=net_data1/net_virt_tx.data firewall_network_virt_tx1.elf

# arp_requester0 receives requests from net1 router but transmits out net1
	$(OBJCOPY) --update-section .net_client_config=net_data1/net_client_arp_requester0.data arp_requester0.elf
	$(OBJCOPY) --update-section .net_client_config=net_data1/net_client_arp_responder1.data arp_responder1.elf
	$(OBJCOPY) --update-section .net_client_config=net_data1/net_client_icmp_filter1.data icmp_filter1.elf
	$(OBJCOPY) --update-section .net_client_config=net_data1/net_client_udp_filter1.data udp_filter1.elf
	$(OBJCOPY) --update-section .net_client_config=net_data1/net_client_tcp_filter1.data tcp_filter1.elf
	$(OBJCOPY) --update-section .net_client_config=net_data1/net_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .int_net_client_config=net_data1/net_client_icmp_module.data icmp_module.elf

	$(OBJCOPY) --update-section .lib_sddf_lwip_config=net_data1/lib_sddf_lwip_config_micropython.data micropython.elf

	$(OBJCOPY) --update-section .serial_client_config=serial_client_arp_responder1.data arp_responder1.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_arp_requester1.data arp_requester1.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_routing1.data routing1.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_micropython.data micropython.elf

	$(OBJCOPY) --update-section .timer_client_config=timer_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_arp_requester1.data arp_requester1.elf

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

qemu: $(IMAGE_FILE)
	$(FIREWALL_SRC_DIR)/docker/scripts/qemu.sh $(IMAGE_FILE) $(QEMU)

FORCE: ;

$(LIONSOS)/dep/micropython/py/mkenv.mk $(LIONSOS)/dep/micropython/mpy-cross:
	cd $(LIONSOS); git submodule update --init dep/micropython
	cd $(LIONSOS)/dep/micropython && git submodule update --init lib/micropython-lib

$(LIONSOS)/dep/libmicrokitco/libmicrokitco.mk:
	cd $(LIONSOS); git submodule update --init dep/libmicrokitco

$(MICRODOT):
	cd $(LIONSOS); git submodule update --init dep/microdot

$(SDDF_MAKEFILES) &:
	cd $(LIONSOS); git submodule update --init dep/sddf

-include $(DEPS)
