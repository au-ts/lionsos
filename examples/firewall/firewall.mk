# Makefile for firewall.
#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This makefile will be copied into the Build directory and used from there.
#
BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
SDDF := $(LIONSOS)/dep/sddf

ifeq ($(strip $(MICROKIT_BOARD)), imx8mp_evk)
	ETH_DRIV_DIR0 := imx
	ETH_DRIV_DIR1 := dwmac-5.10a
	SERIAL_DRIV_DIR := imx
	TIMER_DRV_DIR := imx
	CPU := cortex-a53
else
$(error Unsupported MICROKIT_BOARD given)
endif

TOOLCHAIN := clang
CC := clang
LD := ld.lld
AR := llvm-ar
RANLIB := llvm-ranlib
OBJCOPY := llvm-objcopy
TARGET := aarch64-none-elf
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
PYTHON ?= python3
DTC := dtc

MUSL_SRC := $(LIONSOS)/dep/musllibc
MUSL := musllibc
MICRODOT := ${LIONSOS}/dep/microdot/src
FIREWALL_NET_COMPONENTS := ${FIREWALL_SRC_DIR}/net_components
FIREWALL_FILTERS := ${FIREWALL_SRC_DIR}/filters
FIREWALL_ROUTING := ${FIREWALL_SRC_DIR}/routing
FIREWALL_ARP := ${FIREWALL_SRC_DIR}/arp
FIREWALL_WEBSERVER := ${FIREWALL_SRC_DIR}/webserver

METAPROGRAM := $(FIREWALL_SRC_DIR)/meta.py
DTS := $(SDDF)/dts/$(MICROKIT_BOARD).dts
DTB := $(MICROKIT_BOARD).dtb

SDFGEN_HELPER := $(FIREWALL_SRC_DIR)/sdfgen_helper.py
SDFGEN_UNKOWN_MACROS := ETH_HWADDR_LEN=6 SDDF_NET_MAX_CLIENTS=64
FIREWALL_CONFIG_HEADERS := $(SDDF)/include/sddf/resources/common.h \
							$(SDDF)/include/sddf/resources/device.h \
							$(LIONSOS)/include/lions/firewall/config.h

FIREWALL_IMAGES := arp_requester.elf arp_responder.elf routing.elf \
		  				icmp_filter.elf udp_filter.elf tcp_filter.elf icmp_module.elf

IMAGES := micropython.elf \
		  eth_driver_imx.elf firewall_network_virt_rx.elf firewall_network_virt_tx.elf \
		  eth_driver_dwmac.elf timer_driver.elf serial_driver.elf serial_virt_tx.elf \
		  $(FIREWALL_IMAGES)

DEPS := $(FIREWALL_IMAGES:.elf=.d)

SYSTEM_FILE := firewall.system

CFLAGS := \
	-g \
	-mtune=$(CPU) \
	-mstrict-align \
	-ffreestanding \
	-O2 \
	-MD \
	-MP \
	-Wall \
	-Wno-unused-function \
	-I$(BOARD_DIR)/include \
	-I$(MUSL)/include \
	-target $(TARGET) \
	-DBOARD_$(MICROKIT_BOARD) \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld $(MUSL)/lib/libc.a libsddf_util_debug.a

IMAGE_FILE := loader.img
REPORT_FILE := report.txt

all: $(IMAGE_FILE)
${IMAGES}: libsddf_util_debug.a $(MUSL)/lib/libc.a

CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@

vpath %.c ${SDDF} ${FIREWALL_SRC_DIR} ${FIREWALL_NET_COMPONENTS} ${FIREWALL_FILTERS} ${FIREWALL_ROUTING} ${FIREWALL_ARP} ${FIREWALL_WEBSERVER}

MICROPYTHON_LIBMATH := $(LIBMATH)
MICROPYTHON_EXEC_MODULE := ui_server.py
MICROPYTHON_EXEC_MODULE_PATH := ${FIREWALL_WEBSERVER}/${MICROPYTHON_EXEC_MODULE}
MICROPYTHON_FROZEN_MANIFEST := ${FIREWALL_WEBSERVER}/manifest.py
MICROPYTHON_USER_C_MODULES := $(FIREWALL_WEBSERVER)/modfirewall.c
MICROPYTHON_ENABLE_FIREWALL := 1

include $(LIONSOS)/components/micropython/micropython.mk

${MICROPYTHON_FROZEN_MANIFEST}: ${MICROPYTHON_EXEC_MODULE_PATH}
${MICROPYTHON_EXEC_MODULE_PATH}: $(MICRODOT)

%.py: ${FIREWALL_SRC_DIR}/%.py
	cp $< $@

$(MUSL):
	mkdir -p $@

$(MUSL)/lib/libc.a $(MUSL)/include: ${MUSL_SRC}/Makefile ${MUSL}
	cd ${MUSL} && CC=aarch64-none-elf-gcc CROSS_COMPILE=aarch64-none-elf- ${MUSL_SRC}/configure --srcdir=${MUSL_SRC} --prefix=${abspath ${MUSL}} --target=aarch64 --with-malloc=oldmalloc --enable-warnings --disable-shared --enable-static
	${MAKE} -C ${MUSL} install

%.o: %.c $(MUSL)/include
	${CC} ${CFLAGS} -c -o $@ $<

%.elf: %.o
	$(LD) $(LDFLAGS) $< $(LIBS) -o $@

arp_requester.elf: arp_requester.o libsddf_util.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

arp_responder.elf: arp_responder.o libsddf_util.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

routing.elf: routing.o libsddf_util.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

SDDF_MAKEFILES := ${SDDF}/util/util.mk \
		  ${SDDF}/drivers/network/${ETH_DRIV_DIR0}/eth_driver.mk \
		  ${SDDF}/drivers/network/${ETH_DRIV_DIR1}/eth_driver.mk \
		  ${SDDF}/drivers/serial/${SERIAL_DRIV_DIR}/serial_driver.mk \
		  ${SDDF}/drivers/timer/${TIMER_DRV_DIR}/timer_driver.mk \
		  ${SDDF}/network/components/network_components.mk \
		  ${SDDF}/serial/components/serial_components.mk

include ${SDDF_MAKEFILES}

include ${FIREWALL_NET_COMPONENTS}/firewall_network_components.mk

$(DTB): $(DTS)
	$(DTC) -q -I dts -O dtb $(DTS) > $(DTB)

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	$(PYTHON) $(SDFGEN_HELPER) --macros "$(SDFGEN_UNKOWN_MACROS)" --configs "$(FIREWALL_CONFIG_HEADERS)" --output $(FIREWALL_SRC_DIR)/config_structs.py
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE) --objcopy $(OBJCOPY) --iotgate_idx $(FW_IOTGATE_IDX)
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf

# net0 pds
	$(OBJCOPY) --update-section .device_resources=net_data0/ethernet_driver_dwmac_device_resources.data eth_driver_dwmac.elf
	$(OBJCOPY) --update-section .net_driver_config=net_data0/net_driver.data eth_driver_dwmac.elf

	$(OBJCOPY) --update-section .net_virt_rx_config=net_data0/net_virt_rx.data firewall_network_virt_rx0.elf
	$(OBJCOPY) --update-section .net_virt_tx_config=net_data0/net_virt_tx.data firewall_network_virt_tx0.elf

	$(OBJCOPY) --update-section .net1_client_config=net_data0/net_client_icmp_module.data icmp_module.elf

# arp_requester0 is a net client of the other network
	$(OBJCOPY) --update-section .net_client_config=net_data1/net_client_arp_requester0.data arp_requester0.elf
	$(OBJCOPY) --update-section .net_client_config=net_data0/net_client_arp_responder0.data arp_responder0.elf
	$(OBJCOPY) --update-section .net_client_config=net_data0/net_client_icmp_filter0.data icmp_filter0.elf
	$(OBJCOPY) --update-section .net_client_config=net_data0/net_client_udp_filter0.data udp_filter0.elf
	$(OBJCOPY) --update-section .net_client_config=net_data0/net_client_tcp_filter0.data tcp_filter0.elf

	$(OBJCOPY) --update-section .serial_client_config=serial_client_arp_responder0.data arp_responder0.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_arp_requester0.data arp_requester0.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_routing0.data routing0.elf

	$(OBJCOPY) --update-section .timer_client_config=timer_client_arp_requester0.data arp_requester0.elf

# net1 pds
	$(OBJCOPY) --update-section .device_resources=net_data1/ethernet_driver_imx_device_resources.data eth_driver_imx.elf
	$(OBJCOPY) --update-section .net_driver_config=net_data1/net_driver.data eth_driver_imx.elf

	$(OBJCOPY) --update-section .net_virt_rx_config=net_data1/net_virt_rx.data firewall_network_virt_rx1.elf
	$(OBJCOPY) --update-section .net_virt_tx_config=net_data1/net_virt_tx.data firewall_network_virt_tx1.elf

	$(OBJCOPY) --update-section .net2_client_config=net_data1/net_client_icmp_module.data icmp_module.elf

# arp_requester1 is a net client of the other network
	$(OBJCOPY) --update-section .net_client_config=net_data0/net_client_arp_requester1.data arp_requester1.elf
	$(OBJCOPY) --update-section .net_client_config=net_data1/net_client_arp_responder1.data arp_responder1.elf
	$(OBJCOPY) --update-section .net_client_config=net_data1/net_client_icmp_filter1.data icmp_filter1.elf
	$(OBJCOPY) --update-section .net_client_config=net_data1/net_client_udp_filter1.data udp_filter1.elf
	$(OBJCOPY) --update-section .net_client_config=net_data1/net_client_tcp_filter1.data tcp_filter1.elf
	$(OBJCOPY) --update-section .net_client_config=net_data1/net_client_micropython.data micropython.elf

	$(OBJCOPY) --update-section .serial_client_config=serial_client_arp_responder1.data arp_responder1.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_arp_requester1.data arp_requester1.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_routing1.data routing1.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_micropython.data micropython.elf

	$(OBJCOPY) --update-section .timer_client_config=timer_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_arp_requester1.data arp_requester1.elf

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

qemu: ${IMAGE_FILE}
	$(QEMU) -machine virt,virtualization=on \
			-cpu cortex-a53 \
			-serial mon:stdio \
			-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
			-m size=2G \
			-nographic \
			-device virtio-net-device,netdev=netdev0 \
			-netdev user,id=netdev0,hostfwd=tcp::5555-10.0.2.16:80 \
			-global virtio-mmio.force-legacy=false

FORCE: ;

$(LIONSOS)/dep/micropython/py/mkenv.mk ${LIONSOS}/dep/micropython/mpy-cross:
	cd ${LIONSOS}; git submodule update --init dep/micropython
	cd ${LIONSOS}/dep/micropython && git submodule update --init lib/micropython-lib

${LIONSOS}/dep/libmicrokitco/Makefile:
	cd ${LIONSOS}; git submodule update --init dep/libmicrokitco

${MICRODOT}:
	cd ${LIONSOS}; git submodule update --init dep/microdot

${MUSL_SRC}/Makefile:
	cd ${LIONSOS}; git submodule update --init dep/musllibc

${SDDF_MAKEFILES} &:
	cd ${LIONSOS}; git submodule update --init dep/sddf

-include $(DEPS)
