# Makefile for webserver.
#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This makefile will be copied into the Build directory and used from there.
#
BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
SDDF := $(LIONSOS)/dep/sddf

ifeq (${MICROKIT_BOARD},odroidc4)
	TIMER_DRIVER_DIR := meson
	ETHERNET_DRIVER_DIR := meson
	SERIAL_DRIVER_DIR := meson
	CPU := cortex-a55
else ifeq (${MICROKIT_BOARD},maaxboard)
	TIMER_DRIVER_DIR := imx
	ETHERNET_DRIVER_DIR := imx
	SERIAL_DRIVER_DIR := imx
	CPU := cortex-a53
else ifeq (${MICROKIT_BOARD},qemu_virt_aarch64)
	TIMER_DRIVER_DIR := arm
	ETHERNET_DRIVER_DIR := virtio
	SERIAL_DRIVER_DIR := arm
	CPU := cortex-a53
	QEMU := qemu-system-aarch64
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

NFS=$(LIONSOS)/components/fs/nfs
MUSL_SRC := $(LIONSOS)/dep/musllibc
MUSL := musllibc
MICRODOT := ${LIONSOS}/dep/microdot/src

METAPROGRAM := $(WEBSERVER_SRC_DIR)/meta.py
DTS := $(SDDF)/dts/$(MICROKIT_BOARD).dts
DTB := $(MICROKIT_BOARD).dtb

IMAGES := timer_driver.elf eth_driver.elf micropython.elf nfs.elf \
	  network_copy.elf network_virt_rx.elf network_virt_tx.elf \
	  serial_driver.elf serial_virt_tx.elf

SYSTEM_FILE := webserver.system

CFLAGS := \
	-mtune=$(CPU) \
	-mstrict-align \
	-ffreestanding \
	-O2 \
	-MD \
	-MP \
	-Wall \
	-Wno-unused-function \
	-I$(BOARD_DIR)/include \
	-target $(TARGET) \
	-DBOARD_$(MICROKIT_BOARD) \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a

IMAGE_FILE := webserver.img
REPORT_FILE := report.txt

all: $(IMAGE_FILE)
${IMAGES}: libsddf_util_debug.a

CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@

MICROPYTHON_CONFIG_INCLUDE := $(CONFIG_INCLUDE)
MICROPYTHON_EXEC_MODULE := webserver.py
MICROPYTHON_FROZEN_MANIFEST := manifest.py
include $(LIONSOS)/components/micropython/micropython.mk

manifest.py: webserver.py config.py
webserver.py: $(MICRODOT) config.py

config.py: ${CHECK_FLAGS_BOARD_MD5}
	echo "base_dir='$(WEBSITE_DIR)'" > config.py

%.py: ${WEBSERVER_SRC_DIR}/%.py
	cp $< $@

$(MUSL)/lib/libc.a $(MUSL)/include: ${MUSL_SRC}/Makefile
	mkdir ${MUSL} && cd ${MUSL} && CC=aarch64-none-elf-gcc CROSS_COMPILE=aarch64-none-elf- ${MUSL_SRC}/configure --srcdir=${MUSL_SRC} --prefix=${abspath ${MUSL}} --target=aarch64 --with-malloc=oldmalloc --enable-warnings --disable-shared --enable-static
	${MAKE} -C ${MUSL} install

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

SDDF_MAKEFILES := ${SDDF}/util/util.mk \
		  ${SDDF}/drivers/timer/${TIMER_DRIVER_DIR}/timer_driver.mk \
		  ${SDDF}/drivers/network/${ETHERNET_DRIVER_DIR}/eth_driver.mk \
		  ${SDDF}/drivers/serial/${SERIAL_DRIVER_DIR}/serial_driver.mk \
		  ${SDDF}/network/components/network_components.mk \
		  ${SDDF}/serial/components/serial_components.mk

include ${SDDF_MAKEFILES}
include $(NFS)/nfs.mk

$(DTB): $(DTS)
	$(DTC) -q -I dts -O dtb $(DTS) > $(DTB)

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE) --nfs-server $(NFS_SERVER) --nfs-dir $(NFS_DIRECTORY)
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
