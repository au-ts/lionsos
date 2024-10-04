#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
IMAGES := \
	timer_driver.elf \
	eth_driver.elf \
	micropython.elf \
	fat.elf \
	serial_driver.elf \
	serial_virt_rx.elf \
	serial_virt_tx.elf \
	blk_virt.elf \
	blk_driver.elf

ifeq ($(strip $(MICROKIT_BOARD)), maaxboard)
	BLK_DRIV_DIR := mmc/imx
	SERIAL_DRIV_DIR := imx
	TIMER_DRIV_DIR := imx
	CPU := cortex-a53
else ifeq ($(strip $(MICROKIT_BOARD)), qemu_virt_aarch64)
	BLK_DRIV_DIR := virtio
	SERIAL_DRIV_DIR := arm
	TIMER_DRIV_DIR := arm
	IMAGES += blk_driver.elf
	CPU := cortex-a53
	QEMU := qemu-system-aarch64
else ifeq ($(strip $(MICROKIT_BOARD)), odroidc4)
	NET_DRIV_DIR := meson
	UART_DRIV_DIR := meson
	TIMER_DRIV_DIR := meson
	BLK_DRIV_DIR := sdmmc
	IMAGES += sdmmc_driver.elf
	BLK_MK := sdmmc_driver.mk
	CPU := cortex-a55
else
$(error Unsupported MICROKIT_BOARD given)
endif

TOOLCHAIN := clang
CC := clang
LD := ld.lld
RANLIB := llvm-ranlib
AR := llvm-ar
OBJCOPY := llvm-objcopy
TARGET := aarch64-none-elf
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
DTC := dtc
PYTHON ?= python3

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
SDDF := $(LIONSOS)/dep/sddf

METAPROGRAM := $(FILEIO_DIR)/meta.py
DTS := $(SDDF)/dts/$(MICROKIT_BOARD).dts
DTB := $(MICROKIT_BOARD).dtb

FAT := $(LIONSOS)/components/fs/fat
MUSL_SRC := $(LIONSOS)/dep/musllibc
MUSL := musllibc

CFLAGS := \
	-mtune=$(CPU) \
	-mstrict-align \
	-ffreestanding \
	-g3 \
	-O3 \
	-Wall \
	-Wno-unused-function \
	-I$(BOARD_DIR)/include \
	-target $(TARGET) \
	-DBOARD_$(MICROKIT_BOARD) \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit


LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a

SYSTEM_FILE := fileio.system
IMAGE_FILE := fileio.img
REPORT_FILE := report.txt

all: cache.o
CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@


%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

BLK_DRIVER := $(SDDF)/drivers/blk/${BLK_DRIV_DIR}
BLK_COMPONENTS := $(SDDF)/blk/components

include ${SDDF}/util/util.mk
include ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk
include ${SDDF}/drivers/serial/${SERIAL_DRIV_DIR}/serial_driver.mk
include ${SDDF}/serial/components/serial_components.mk
include ${SDDF}/libco/libco.mk
include ${BLK_DRIVER}/blk_driver.mk
include ${BLK_COMPONENTS}/blk_components.mk

MICROPYTHON_LIBMATH := ${LIBMATH}
MICROPYTHON_CONFIG_INCLUDE := ${CONFIG_INCLUDE}
MICROPYTHON_FROZEN_MANIFEST := manifest.py
include $(LIONSOS)/components/micropython/micropython.mk

manifest.py: fs_test.py bench.py

%.py: ${FILEIO_DIR}/%.py
	cp $< $@

FAT_LIBC_LIB := musllibc/lib/libc.a
FAT_LIBC_INCLUDE := musllibc/include
include $(LIONSOS)/components/fs/fat/fat.mk

$(MUSL):
	mkdir -p $@

$(MUSL)/lib/libc.a $(MUSL)/include: ${MUSL}
	cd ${MUSL} && CC=aarch64-none-elf-gcc CROSS_COMPILE=aarch64-none-elf- ${MUSL_SRC}/configure --srcdir=${MUSL_SRC} --prefix=${abspath ${MUSL}} --target=aarch64 --with-malloc=oldmalloc --enable-warnings --disable-shared --enable-static
	${MAKE} -C ${MUSL} install

${IMAGES}: libsddf_util_debug.a

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

FORCE:

%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

mpy-cross: FORCE
	${MAKE} -C ${LIONSOS}/dep/micropython/mpy-cross BUILD=$(abspath ./mpy_cross)

$(DTB): $(DTS)
	$(DTC) -q -I dts -O dtb $(DTS) > $(DTB)

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE)
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .serial_virt_rx_config=serial_virt_rx.data serial_virt_rx.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .device_resources=blk_driver_device_resources.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_driver_config=blk_driver.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_virt_config=blk_virt.data blk_virt.elf
	$(OBJCOPY) --update-section .blk_client_config=blk_client_fatfs.data fat.elf
	$(OBJCOPY) --update-section .fs_server_config=fs_server_fatfs.data fat.elf

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

qemu_disk:
	$(LIONSOS)/dep/sddf/tools/mkvirtdisk $@ 1 512 16777216

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
		-device virtio-blk-device,drive=hd
