#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

TOOLCHAIN ?= clang
SUPPORTED_BOARDS := \
	qemu_virt_aarch64 \
	maaxboard

IMAGES := \
	timer_driver.elf \
	fileio.elf \
	tcp_server.elf \
	fat.elf \
	serial_driver.elf \
	serial_virt_rx.elf \
	serial_virt_tx.elf \
	eth_driver.elf \
	network_virt_tx.elf \
	network_virt_rx.elf \
	network_copy.elf \
	blk_virt.elf \
	blk_driver.elf

TOOLCHAIN ?= clang
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
SDDF := $(LIONSOS)/dep/sddf
LWIP := $(SDDF)/network/ipstacks/lwip/src
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco
WAMR := $(LIONSOS)/components/wamr

SYSTEM_FILE := wasm.system
IMAGE_FILE := wasm.img
REPORT_FILE := report.txt

all: ${IMAGE_FILE}

include ${SDDF}/tools/make/board/common.mk

METAPROGRAM := $(WASM_DIR)/meta.py

FAT := $(LIONSOS)/components/fs/fat

CFLAGS += \
	-Wno-bitwise-op-parentheses \
	-Wno-shift-op-parentheses \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(LIBMICROKITCO_PATH) \
	-I$(LWIP)/include

include $(LIONSOS)/lib/libc/libc.mk

LDFLAGS := -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a -lc

BLK_DRIVER := $(SDDF)/drivers/blk/${BLK_DRIV_DIR}
BLK_COMPONENTS := $(SDDF)/blk/components

WASM_TARGETS := fileio tcp_server
include $(WAMR)/wamr.mk

SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include
FAT_LIBC_LIB := $(LIONS_LIBC)/lib/libc.a
FAT_LIBC_INCLUDE := $(LIONS_LIBC)/include

SDDF_MAKEFILES := ${SDDF}/util/util.mk \
${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk \
${SDDF}/drivers/serial/${UART_DRIV_DIR}/serial_driver.mk \
${SDDF}/drivers/network/${NET_DRIV_DIR}/eth_driver.mk \
${SDDF}/serial/components/serial_components.mk \
${SDDF}/network/components/network_components.mk \
${SDDF}/network/lib_sddf_lwip/lib_sddf_lwip.mk \
${SDDF}/libco/libco.mk \
${BLK_DRIVER}/blk_driver.mk \
${BLK_COMPONENTS}/blk_components.mk

include ${SDDF_MAKEFILES}
include $(LIONSOS)/components/fs/fat/fat.mk
include $(LIBMICROKITCO_PATH)/libmicrokitco.mk

${IMAGES}: $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a

fileio.wasm: $(WASM_DIR)/fileio.c
	${WASI_SDK}/bin/clang -O3 \
        -z stack-size=4096 -Wl,--initial-memory=65536 \
        -o $@ $< \
        -Wl,--export=__data_end -Wl,--export=__heap_base \
        -Wl,--strip-all

WAMR_SOCKET := $(WAMR_ROOT)/core/iwasm/libraries/lib-socket
tcp_server.wasm: $(WASM_DIR)/tcp_server.c $(WAMR_SOCKET)/src/wasi/wasi_socket_ext.c
	${WASI_SDK}/bin/clang -O3 \
        -z stack-size=4096 -Wl,--initial-memory=65536 \
        -o $@ $^ \
        -Wl,--export=__data_end -Wl,--export=__heap_base \
        -Wl,--strip-all \
		-I$(WAMR_SOCKET)/inc

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

$(DTB): $(DTS)
	$(DTC) -q -I dts -O dtb $(DTS) > $(DTB)

FORCE:

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	PYTHONPATH=${SDDF}/tools/meta:$$PYTHONPATH $(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE)
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .serial_virt_rx_config=serial_virt_rx.data serial_virt_rx.elf
	$(OBJCOPY) --update-section .device_resources=ethernet_driver_device_resources.data eth_driver.elf
	$(OBJCOPY) --update-section .net_driver_config=net_driver.data eth_driver.elf
	$(OBJCOPY) --update-section .net_virt_rx_config=net_virt_rx.data network_virt_rx.elf
	$(OBJCOPY) --update-section .net_virt_tx_config=net_virt_tx.data network_virt_tx.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_tcp_server_net_copier.data network_copy.elf network_copy_tcp_server.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .device_resources=blk_driver_device_resources.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_driver_config=blk_driver.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_virt_config=blk_virt.data blk_virt.elf
	$(OBJCOPY) --update-section .blk_client_config=blk_client_fatfs.data fat.elf
	$(OBJCOPY) --update-section .fs_server_config=fs_server_fatfs.data fat.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_fileio.data fileio.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_fileio.data fileio.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_fileio.data fileio.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_tcp_server.data tcp_server.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_tcp_server.data tcp_server.elf
	$(OBJCOPY) --update-section .lib_sddf_lwip_config=lib_sddf_lwip_config_tcp_server.data tcp_server.elf
	$(OBJCOPY) --update-section .net_client_config=net_client_tcp_server.data tcp_server.elf

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

qemu_disk:
	$(SDDF)/tools/mkvirtdisk $@ 1 512 16777216 GPT

qemu: ${IMAGE_FILE} qemu_disk
	$(QEMU) -machine virt,virtualization=on \
		-cpu cortex-a53 \
		-serial mon:stdio \
		-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
		-m size=2G \
		-nographic \
		-global virtio-mmio.force-legacy=false \
		-d guest_errors \
		-drive file=qemu_disk,if=none,format=raw,id=hd \
		-device virtio-blk-device,drive=hd \
		-device virtio-net-device,netdev=netdev0 \
		-netdev user,id=netdev0,hostfwd=tcp::5555-10.0.2.15:1234

${SDDF_MAKEFILES} &:
	cd ${LIONSOS}; git submodule update --init dep/sddf

${LIONSOS}/dep/libmicrokitco/libmicrokitco.mk:
	cd ${LIONSOS}; git submodule update --init dep/libmicrokitco

${SDDF}/tools/make/board/common.mk ${SDDF_MAKEFILES} ${LIONSOS}/dep/sddf/include &:
	cd ${LIONSOS}; git submodule update --init dep/sddf
