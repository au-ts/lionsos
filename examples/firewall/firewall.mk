#
# Copyright 2022, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

QEMU := qemu-system-aarch64
DTC := dtc
PYTHON ?= python3

METAPROGRAM := $(TOP)/meta.py

MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
SDDF := $(LIONSOS)/dep/sddf
FIREWALL := $(LIONSOS)/examples/firewall
FIREWALL_COMPONENTS := $(FIREWALL)/components
TARGET := aarch64-none-elf

UTIL:=$(SDDF)/util
ETHERNET_DRIVER0:=$(SDDF)/drivers/network/$(ETH_DRIV_DIR0)
ETHERNET_DRIVER1:=$(SDDF)/drivers/network/$(ETH_DRIV_DIR1)
SERIAL_COMPONENTS := $(SDDF)/serial/components
SERIAL_DRIVER := $(SDDF)/drivers/serial/$(SERIAL_DRIV_DIR)
TIMER_DRIVER:=$(SDDF)/drivers/timer/$(TIMER_DRV_DIR)
NETWORK_COMPONENTS:=$(SDDF)/network/components

MUSL_SRC := $(LIONSOS)/dep/musllibc
MUSL := musllibc
MICRODOT := ${LIONSOS}/dep/microdot/src

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
IMAGE_FILE := loader.img
REPORT_FILE := report.txt
SYSTEM_FILE := firewall.system
DTS := $(SDDF)/dts/$(MICROKIT_BOARD).dts
DTB := $(MICROKIT_BOARD).dtb

vpath %.c ${SDDF} ${FIREWALL} ${FIREWALL_COMPONENTS}

IMAGES := eth_driver_imx.elf firewall_network_virt_rx.elf firewall_network_virt_tx.elf \
		  eth_driver_dwmac.elf timer_driver.elf serial_driver.elf serial_virt_tx.elf \
		  arp_requester.elf arp_responder.elf routing.elf \
		  icmp_filter.elf udp_filter.elf tcp_filter.elf \

CFLAGS := -mcpu=$(CPU) \
	  -mstrict-align \
	  -ffreestanding \
	  -g3 -O3 -Wall \
	  -Wno-unused-function \
	  -DMICROKIT_CONFIG_$(MICROKIT_CONFIG) \
	  -I$(BOARD_DIR)/include \
	  -I$(SDDF)/include \
	  -I$(FIREWALL)/include \
	  -I$(LIONSOS)/include \
	  -DBOARD_$(MICROKIT_BOARD) \
	  -MD \
	  -MP

LDFLAGS := -L$(BOARD_DIR)/lib -L${LIBC}
LIBS := --start-group -lmicrokit -Tmicrokit.ld -lc libsddf_util_debug.a --end-group

CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@

%.elf: %.o
	$(LD) $(LDFLAGS) $< $(LIBS) -o $@

arp_requester.elf: arp_requester.o libsddf_util.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

arp_responder.elf: arp_responder.o libsddf_util.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

routing.elf: routing.o libsddf_util.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

all: $(IMAGE_FILE)

# Need to build libsddf_util_debug.a because it's included in LIBS
# for the unimplemented libc dependencies
${IMAGES}: libsddf_util_debug.a

$(DTB): $(DTS)
	dtc -q -I dts -O dtb $(DTS) > $(DTB)

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE) --objcopy $(OBJCOPY)
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf

	$(OBJCOPY) --update-section .device_resources=net_data0/ethernet_driver_imx_device_resources.data eth_driver_imx.elf
	$(OBJCOPY) --update-section .net_driver_config=net_data0/net_driver.data eth_driver_imx.elf

	$(OBJCOPY) --update-section .net_virt_rx_config=net_data0/net_virt_rx.data firewall_network_virt_rx0.elf
	$(OBJCOPY) --update-section .net_virt_tx_config=net_data0/net_virt_tx.data firewall_network_virt_tx0.elf

# arp_requester0 is a net client of the other network
	$(OBJCOPY) --update-section .net_client_config=net_data1/net_client_arp_requester0.data arp_requester0.elf
	$(OBJCOPY) --update-section .net_client_config=net_data0/net_client_arp_responder0.data arp_responder0.elf
	$(OBJCOPY) --update-section .net_client_config=net_data0/net_client_icmp_filter0.data icmp_filter0.elf
	$(OBJCOPY) --update-section .net_client_config=net_data0/net_client_udp_filter0.data udp_filter0.elf
	$(OBJCOPY) --update-section .net_client_config=net_data0/net_client_tcp_filter0.data tcp_filter0.elf

	$(OBJCOPY) --update-section .serial_client_config=serial_client_arp_responder0.data arp_responder0.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_arp_requester0.data arp_requester0.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_routing0.data routing0.elf

	$(OBJCOPY) --update-section .timer_client_config=timer_client_arp_requester1.data arp_requester1.elf

	$(OBJCOPY) --update-section .device_resources=net_data1/ethernet_driver_dwmac_device_resources.data eth_driver_dwmac.elf
	$(OBJCOPY) --update-section .net_driver_config=net_data1/net_driver.data eth_driver_dwmac.elf

	$(OBJCOPY) --update-section .net_virt_rx_config=net_data1/net_virt_rx.data firewall_network_virt_rx1.elf
	$(OBJCOPY) --update-section .net_virt_tx_config=net_data1/net_virt_tx.data firewall_network_virt_tx1.elf

# arp_requester1 is a net client of the other network

	$(OBJCOPY) --update-section .net_client_config=net_data0/net_client_arp_requester1.data arp_requester1.elf
	$(OBJCOPY) --update-section .net_client_config=net_data1/net_client_arp_responder1.data arp_responder1.elf
	$(OBJCOPY) --update-section .net_client_config=net_data1/net_client_icmp_filter1.data icmp_filter1.elf

	$(OBJCOPY) --update-section .serial_client_config=serial_client_arp_responder1.data arp_responder1.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_arp_requester1.data arp_requester1.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_routing1.data routing1.elf

${IMAGE_FILE} $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)


include ${SDDF}/util/util.mk
include ${FIREWALL_COMPONENTS}/firewall_network_components.mk
include ${ETHERNET_DRIVER0}/eth_driver.mk
include ${ETHERNET_DRIVER1}/eth_driver.mk
include ${TIMER_DRIVER}/timer_driver.mk
include ${SERIAL_DRIVER}/serial_driver.mk
include ${SERIAL_COMPONENTS}/serial_components.mk

qemu: $(IMAGE_FILE)
	$(QEMU) -machine virt,virtualization=on \
			-cpu cortex-a53 \
			-serial mon:stdio \
			-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
			-m size=2G \
			-nographic \
			-device virtio-net-device,netdev=netdev0 \
			-netdev user,id=netdev0,hostfwd=tcp::1236-:1236,hostfwd=tcp::1237-:1237,hostfwd=udp::1235-:1235 \
			-global virtio-mmio.force-legacy=false \
			-d guest_errors

-include $(DEPS)
