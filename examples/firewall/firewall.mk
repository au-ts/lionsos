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
ETHERNET_DRIVER:=$(SDDF)/drivers/network/$(DRIV_DIR)
ETHERNET_DRIVER_DWMAC:=$(SDDF)/drivers/network/dwmac-5.10a
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

IMAGES := eth_driver.elf firewall_network_virt_rx.elf network_virt_tx.elf network_copy.elf \
		  eth_driver_dwmac.elf network_virt_rx_1.elf firewall_network_virt_rx_1.elf \
		  timer_driver.elf serial_driver.elf serial_virt_tx.elf \
		  arp_requester.elf arp_responder.elf routing.elf \
		  arp_requester2.elf arp_responder2.elf routing2.elf \
		  icmp_filter.elf udp_filter.elf tcp_filter.elf \
		  icmp_filter2.elf

CFLAGS := -mcpu=$(CPU) \
	  -mstrict-align \
	  -ffreestanding \
	  -g3 -O3 -Wall \
	  -Wno-unused-function \
	  -DMICROKIT_CONFIG_$(MICROKIT_CONFIG) \
	  -I$(BOARD_DIR)/include \
	  -I$(SDDF)/include \
	  -I${FIREWALL}/include \
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

all: $(IMAGE_FILE)

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE)
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .device_resources=ethernet_driver_device_resources.data eth_driver.elf
	$(OBJCOPY) --update-section .net_driver_config=net_ethernet_driver.data eth_driver.elf
	$(OBJCOPY) --update-section .device_resources=ethernet_driver_dwmac_device_resources.data eth_driver_dwmac.elf
	$(OBJCOPY) --update-section .net_driver_config=net_ethernet_driver_dwmac.data eth_driver_dwmac.elf

	$(OBJCOPY) --update-section .net_virt_rx_config=net_net_virt_rx.data firewall_network_virt_rx.elf
	$(OBJCOPY) --update-section .net_virt_rx_config=net_net_virt_rx_1.data firewall_network_virt_rx.elf firewall_network_virt_rx_1.elf
	$(OBJCOPY) --update-section .net_virt_tx_config=net_net_virt_tx.data network_virt_tx.elf
	$(OBJCOPY) --update-section .net_virt_tx_config=net_net_virt_tx_1.data network_virt_tx.elf network_virt_tx_1.elf

	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf

	$(OBJCOPY) --update-section .net_client_config=net_ethernet_driver_dwmac_client_arp_responder1.data arp_responder.elf
	$(OBJCOPY) --update-section .arp_resources=arp_responder1.data arp_responder.elf
	$(OBJCOPY) --update-section .net_client_config=net_ethernet_driver_client_arp_responder2.data arp_responder.elf arp_responder2.elf
	$(OBJCOPY) --update-section .arp_resources=arp_responder2.data arp_responder2.elf

	$(OBJCOPY) --update-section .net_client_config=net_ethernet_driver_client_routing1.data routing.elf
	$(OBJCOPY) --update-section .router_config=router1.data routing.elf
	$(OBJCOPY) --update-section .net_client_config=net_ethernet_driver_dwmac_client_routing2.data routing.elf routing2.elf
	$(OBJCOPY) --update-section .router_config=router2.data routing2.elf

	$(OBJCOPY) --update-section .net_client_config=net_ethernet_driver_client_arp_requester1.data arp_requester.elf
	$(OBJCOPY) --update-section .arp_resources=arp_requester1.data arp_requester.elf
	$(OBJCOPY) --update-section .net_client_config=net_ethernet_driver_dwmac_client_arp_requester2.data arp_requester.elf arp_requester2.elf
	$(OBJCOPY) --update-section .arp_resources=arp_requester2.data arp_requester2.elf

	$(OBJCOPY) --update-section .filter_config=firewall_filters1_icmp_filter.data icmp_filter.elf
	$(OBJCOPY) --update-section .net_client_config=net_ethernet_driver_dwmac_client_icmp_filter.data icmp_filter.elf
	$(OBJCOPY) --update-section .filter_config=firewall_filters2_icmp_filter2.data icmp_filter.elf icmp_filter2.elf
	$(OBJCOPY) --update-section .net_client_config=net_ethernet_driver_client_icmp_filter2.data icmp_filter2.elf

	$(OBJCOPY) --update-section .filter_config=firewall_filters1_udp_filter.data udp_filter.elf
	$(OBJCOPY) --update-section .net_client_config=net_ethernet_driver_dwmac_client_udp_filter.data udp_filter.elf

	$(OBJCOPY) --update-section .filter_config=firewall_filters1_tcp_filter.data tcp_filter.elf
	$(OBJCOPY) --update-section .net_client_config=net_ethernet_driver_dwmac_client_tcp_filter.data tcp_filter.elf

${IMAGE_FILE} $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)


include ${SDDF}/util/util.mk
include ${SDDF}/network/components/network_components.mk
include ${ETHERNET_DRIVER}/eth_driver.mk
include ${ETHERNET_DRIVER_DWMAC}/eth_driver.mk
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
