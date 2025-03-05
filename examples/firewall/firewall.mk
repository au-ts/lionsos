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
FIREWALL_GUI_DIR := $(FIREWALL)/web_ui
TARGET := aarch64-none-elf

UTIL:=$(SDDF)/util
ETHERNET_DRIVER:=$(SDDF)/drivers/network/$(DRIV_DIR)
ETHERNET_DRIVER_DWMAC:=$(SDDF)/drivers/network/dwmac-5.10a
SERIAL_COMPONENTS := $(SDDF)/serial/components
UART_DRIVER := $(SDDF)/drivers/serial/$(UART_DRIV_DIR)
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
		  timer_driver.elf uart_driver.elf serial_virt_tx.elf \
		  arp_requester.elf arp_responder.elf routing.elf \
		  arp_requester2.elf arp_responder2.elf routing2.elf \
		  icmp_filter.elf udp_filter.elf tcp_filter.elf \
		  icmp_filter2.elf micropython.elf

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

micropython.elf: mpy-cross manifest.py ui_server.py \
		${MICRODOT} ${LIONSOS}/dep/libmicrokitco/Makefile
	make -C $(LIONSOS)/components/micropython -j$(nproc) \
			MICROKIT_SDK=$(MICROKIT_SDK) \
			MICROKIT_BOARD=$(MICROKIT_BOARD) \
			MICROKIT_CONFIG=$(MICROKIT_CONFIG) \
			MICROPY_MPYCROSS=$(abspath mpy_cross/mpy-cross) \
			MICROPY_MPYCROSS_DEPENDENCY=$(abspath mpy_cross/mpy-cross) \
			BUILD=$(abspath .) \
			LIBMATH=$(LIBMATH) \
			LIBMATH=$(abspath $(BUILD_DIR)/libm) \
			FROZEN_MANIFEST=$(abspath ./manifest.py) \
			EXEC_MODULE=ui_server.py \
			USER_C_MODULES=$(FIREWALL_GUI_DIR)/modfirewall.c

%.py: ${FIREWALL_GUI_DIR}/%.py
	cp $< $@

$(MUSL)/lib/libc.a $(MUSL)/include: ${MUSL}/Makefile
	make -C $(MUSL_SRC) \
		C_COMPILER=aarch64-none-elf-gcc \
		TOOLPREFIX=aarch64-none-elf- \
		CONFIG_ARCH_AARCH64=y \
		STAGE_DIR=$(abspath $(MUSL)) \
		SOURCE_DIR=.

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

FORCE: ;


mpy-cross: FORCE  ${LIONSOS}/dep/micropython/mpy-cross
	make -C $(LIONSOS)/dep/micropython/mpy-cross BUILD=$(abspath ./mpy_cross)

# Need to build libsddf_util_debug.a because it's included in LIBS
# for the unimplemented libc dependencies
${IMAGES}: libsddf_util_debug.a

$(DTB): $(DTS)
	dtc -q -I dts -O dtb $(DTS) > $(DTB)

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE)
	$(OBJCOPY) --update-section .device_resources=uart_driver_device_resources.data uart_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data uart_driver.elf
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
include ${UART_DRIVER}/uart_driver.mk
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

$(LIONSOS)/dep/micropython/py/mkenv.mk ${LIONSOS}/dep/micropython/mpy-cross:
	cd ${LIONSOS}; git submodule update --init dep/micropython
	cd ${LIONSOS}/dep/micropython && git submodule update --init lib/micropython-lib
${LIONSOS}/dep/libmicrokitco/Makefile:
	cd ${LIONSOS}; git submodule update --init dep/libmicrokitco


clean::
	${RM} -f *.elf .depend* $
	find . -name \*.[do] |xargs --no-run-if-empty rm

clobber:: clean
	rm -f *.a
	rm -f ${IMAGE_FILE} ${REPORT_FILE}

-include $(DEPS)
