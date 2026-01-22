#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

TOOLCHAIN ?= clang
SUPPORTED_BOARDS := \
	qemu_virt_aarch64 \
	maaxboard

IMAGES := \
	timer_driver.elf \
	test_core.elf \
	test_file.elf \
	test_server.elf \
	test_client.elf \
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
SYSTEM_FILE := posix_test.system
IMAGE_FILE := posix_test.img
REPORT_FILE := report.txt

all: ${IMAGE_FILE}

include ${SDDF}/tools/make/board/common.mk

METAPROGRAM := $(POSIX_TEST_DIR)/meta.py

FAT := $(LIONSOS)/components/fs/fat

CFLAGS += \
	-Wno-bitwise-op-parentheses \
	-Wno-shift-op-parentheses \
	-Wno-unused-function \
	-Wno-tautological-constant-out-of-range-compare \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(LIBMICROKITCO_PATH) \
	-I$(LWIP)/include \
	-DMAX_FDS=8

include $(LIONSOS)/lib/libc/libc.mk

LDFLAGS := -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a -lc

BLK_DRIVER := $(SDDF)/drivers/blk/${BLK_DRIV_DIR}
BLK_COMPONENTS := $(SDDF)/blk/components

SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include
include ${SDDF}/util/util.mk
include ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk
include ${SDDF}/drivers/serial/${UART_DRIV_DIR}/serial_driver.mk
include ${SDDF}/drivers/network/${NET_DRIV_DIR}/eth_driver.mk
include ${SDDF}/serial/components/serial_components.mk
include ${SDDF}/network/components/network_components.mk

LIB_SDDF_LWIP_CFLAGS := -I${POSIX_TEST_DIR}/lwip_include
include ${SDDF}/network/lib_sddf_lwip/lib_sddf_lwip.mk

include ${SDDF}/libco/libco.mk
include ${BLK_DRIVER}/blk_driver.mk
include ${BLK_COMPONENTS}/blk_components.mk

FAT_LIBC_LIB := $(LIONS_LIBC)/lib/libc.a
FAT_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIONSOS)/components/fs/fat/fat.mk

LIBMICROKITCO_CFLAGS_posix_test := -I$(POSIX_TEST_DIR)
LIBMICROKITCO_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIBMICROKITCO_PATH)/libmicrokitco.mk

${IMAGES}: $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a

# for libmicrokitco_opts.h and lwipopts.h
tcp.o test_core.o test_file.o test_server.o test_client.o: CFLAGS += $(LIBMICROKITCO_CFLAGS_posix_test)
tcp.o test_server.o test_client.o: CFLAGS += $(LIB_SDDF_LWIP_CFLAGS)

tcp.o: $(LIONSOS)/lib/sock/tcp.c | $(LIONS_LIBC)/include
	${CC} ${CFLAGS} -c -o $@ $<

test_core.o: $(POSIX_TEST_DIR)/test_core.c | $(LIONS_LIBC)/include
	${CC} ${CFLAGS} -c -o $@ $<

test_core.elf: test_core.o libmicrokitco_posix_test.a
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

test_file.o: $(POSIX_TEST_DIR)/test_file.c | $(LIONS_LIBC)/include
	${CC} ${CFLAGS} -c -o $@ $<

test_file.elf: test_file.o libmicrokitco_posix_test.a
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

test_server.o: $(POSIX_TEST_DIR)/test_server.c | $(LIONS_LIBC)/include
	${CC} ${CFLAGS} -c -o $@ $<

test_server.elf: test_server.o libmicrokitco_posix_test.a tcp.o lib_sddf_lwip.a
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

test_client.o: $(POSIX_TEST_DIR)/test_client.c | $(LIONS_LIBC)/include
	${CC} ${CFLAGS} -c -o $@ $<

test_client.elf: test_client.o libmicrokitco_posix_test.a tcp.o lib_sddf_lwip.a
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

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
	$(OBJCOPY) --update-section .net_copy_config=net_copy_test_server_copier.data network_copy.elf network_copy_test_server.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_test_client_copier.data network_copy.elf network_copy_test_client.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .device_resources=blk_driver_device_resources.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_driver_config=blk_driver.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_virt_config=blk_virt.data blk_virt.elf
	$(OBJCOPY) --update-section .blk_client_config=blk_client_fatfs.data fat.elf
	$(OBJCOPY) --update-section .fs_server_config=fs_server_fatfs.data fat.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_test_core.data test_core.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_test_core.data test_core.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_test_file.data test_file.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_test_file.data test_file.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_test_file.data test_file.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_test_server.data test_server.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_test_server.data test_server.elf
	$(OBJCOPY) --update-section .lib_sddf_lwip_config=lib_sddf_lwip_config_test_server.data test_server.elf
	$(OBJCOPY) --update-section .net_client_config=net_client_test_server.data test_server.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_test_client.data test_client.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_test_client.data test_client.elf
	$(OBJCOPY) --update-section .lib_sddf_lwip_config=lib_sddf_lwip_config_test_client.data test_client.elf
	$(OBJCOPY) --update-section .net_client_config=net_client_test_client.data test_client.elf

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
		-netdev user,id=netdev0,hostfwd=tcp::5560-10.0.2.15:5560,hostfwd=tcp::5561-10.0.2.15:5561
