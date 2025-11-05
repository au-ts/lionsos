# Makefile for webserver.
#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This makefile will be copied into the Build directory and used from there.
#
SDDF := $(LIONSOS)/dep/sddf
LWIP := $(SDDF)/network/ipstacks/lwip/src
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco

SUPPORTED_BOARDS := \
	odroidc4 \
	maaxboard \
	qemu_virt_aarch64

TOOLCHAIN ?= clang

IMAGE_FILE := webserver.img
REPORT_FILE := report.txt

all: $(IMAGE_FILE)

include ${SDDF}/tools/make/board/common.mk

NFS=$(LIONSOS)/components/fs/nfs
MICRODOT := ${LIONSOS}/dep/microdot/src

METAPROGRAM := $(WEBSERVER_SRC_DIR)/meta.py

IMAGES := timer_driver.elf eth_driver.elf micropython.elf nfs.elf \
	  network_copy.elf network_virt_rx.elf network_virt_tx.elf \
	  serial_driver.elf serial_virt_tx.elf

SYSTEM_FILE := webserver.system

CFLAGS += \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit
include $(LIONSOS)/lib/libc/libc.mk

LDFLAGS := -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib
LIBS := -lmicrokit -Tmicrokit.ld -lc libsddf_util_debug.a
${IMAGES}: $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a


MICROPYTHON_LIBMATH := $(LIBMATH)
MICROPYTHON_EXEC_MODULE := webserver.py
MICROPYTHON_FROZEN_MANIFEST := manifest.py
include $(LIONSOS)/components/micropython/micropython.mk

manifest.py: webserver.py config.py
webserver.py: $(MICRODOT) config.py

config.py: ${CHECK_FLAGS_BOARD_MD5}
	echo "base_dir='$(WEBSITE_DIR)'" > config.py

%.py: ${WEBSERVER_SRC_DIR}/%.py
	cp $< $@

include $(NFS)/nfs.mk

SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include

SDDF_MAKEFILES := ${SDDF}/util/util.mk \
		  ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk \
		  ${SDDF}/drivers/network/${NET_DRIV_DIR}/eth_driver.mk \
		  ${SDDF}/drivers/serial/${UART_DRIV_DIR}/serial_driver.mk \
		  ${SDDF}/network/components/network_components.mk \
		  ${SDDF}/network/lib_sddf_lwip/lib_sddf_lwip.mk \
		  ${SDDF}/serial/components/serial_components.mk

include ${SDDF_MAKEFILES}

LIBMICROKITCO_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIBMICROKITCO_PATH)/libmicrokitco.mk

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	PYTHONPATH=${SDDF}/tools/meta:$$PYTHONPATH $(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE) --nfs-server $(NFS_SERVER) --nfs-dir $(NFS_DIRECTORY)
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .device_resources=ethernet_driver_device_resources.data eth_driver.elf
	$(OBJCOPY) --update-section .net_driver_config=net_driver.data eth_driver.elf
	$(OBJCOPY) --update-section .net_virt_rx_config=net_virt_rx.data network_virt_rx.elf
	$(OBJCOPY) --update-section .net_virt_tx_config=net_virt_tx.data network_virt_tx.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_micropython_net_copier.data network_copy.elf network_copy_micropython.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_nfs_net_copier.data network_copy.elf network_copy_nfs.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .net_client_config=net_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .net_client_config=net_client_nfs.data nfs.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_nfs.data nfs.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_nfs.data nfs.elf
	$(OBJCOPY) --update-section .fs_server_config=fs_server_nfs.data nfs.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .nfs_config=nfs_config.data nfs.elf
	$(OBJCOPY) --update-section .lib_sddf_lwip_config=lib_sddf_lwip_config_nfs.data nfs.elf
	$(OBJCOPY) --update-section .lib_sddf_lwip_config=lib_sddf_lwip_config_micropython.data micropython.elf
	touch $@

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

$(LIONSOS)/dep/micropython/py/mkenv.mk ${LIONSOS}/dep/micropython/mpy-cross &:
	cd ${LIONSOS}; git submodule update --init dep/micropython
	cd ${LIONSOS}/dep/micropython && git submodule update --init lib/micropython-lib

${LIONSOS}/dep/libmicrokitco/libmicrokitco.mk:
	cd ${LIONSOS}; git submodule update --init dep/libmicrokitco

${MICRODOT}:
	cd ${LIONSOS}; git submodule update --init dep/microdot

${SDDF}/tools/make/board/common.mk ${SDDF_MAKEFILES} ${LIONSOS}/dep/sddf/include &:
	cd ${LIONSOS}; git submodule update --init dep/sddf
