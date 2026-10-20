# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause

SUPPORTED_BOARDS := qemu_virt_aarch64
TOOLCHAIN ?= clang
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
SDDF := $(LIONSOS)/dep/sddf
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco
SYSTEM_FILE := fio_mux.system
IMAGE_FILE := fio_mux.img
REPORT_FILE := report.txt

IMAGES := \
	client0.elf \
	client1.elf \
	fs_multiplexer.elf \
	fat_mux.elf \
	serial_driver.elf \
	serial_virt_tx.elf \
	blk_virt.elf \
	blk_driver.elf

all: $(IMAGE_FILE)

include $(SDDF)/tools/make/board/common.mk

METAPROGRAM := $(FIO_MUX_DIR)/meta.py
CFLAGS += \
	-I$(FIO_MUX_DIR) \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(LIBMICROKITCO_PATH) \
	-DMAX_FDS=8

include $(LIONSOS)/lib/libc/libc.mk

LDFLAGS := -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a -lc

BLK_DRIVER := $(SDDF)/drivers/blk/$(BLK_DRIV_DIR)
BLK_COMPONENTS := $(SDDF)/blk/components
SDDF_CUSTOM_LIBC := 1
SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(SDDF)/util/util.mk
include $(SDDF)/drivers/serial/$(UART_DRIV_DIR)/serial_driver.mk
include $(SDDF)/serial/components/serial_components.mk
include $(SDDF)/libco/libco.mk
include $(BLK_DRIVER)/blk_driver.mk
include $(BLK_COMPONENTS)/blk_components.mk

FAT_LIBC_LIB := $(LIONS_LIBC)/lib/libc.a
FAT_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIONSOS)/components/fs/fat/fat.mk

include $(LIONSOS)/components/fs/multiplexer/multiplexer.mk

LIBMICROKITCO_CFLAGS_fio_mux := -I$(FIO_MUX_DIR)
LIBMICROKITCO_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIBMICROKITCO_PATH)/libmicrokitco.mk

$(IMAGES): $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a

client0.o: $(FIO_MUX_DIR)/client.c | $(LIONS_LIBC)/include
	$(CC) $(CFLAGS) -DCLIENT_ID=0 -c -o $@ $<

client1.o: $(FIO_MUX_DIR)/client.c | $(LIONS_LIBC)/include
	$(CC) $(CFLAGS) -DCLIENT_ID=1 -c -o $@ $<

client0.elf: client0.o libmicrokitco_fio_mux.a
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

client1.elf: client1.o libmicrokitco_fio_mux.a
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	PYTHONPATH=$(SDDF)/tools/meta:$$PYTHONPATH $(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $@
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_client0.data client0.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_client1.data client1.elf
	$(OBJCOPY) --update-section .device_resources=blk_driver_device_resources.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_driver_config=blk_driver.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_virt_config=blk_virt.data blk_virt.elf
	$(OBJCOPY) --update-section .blk_client_config=blk_client_fatfs.data fat_mux.elf
	$(OBJCOPY) --update-section .fs_multiplexer_config=fs_multiplexer_mux0.data fs_multiplexer.elf
	$(OBJCOPY) --update-section .fs_server_config=fs_server_fatfs.data fat_mux.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_client0_fatfs.data client0.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_client1_fatfs.data client1.elf
	touch $@

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

# The ping/pong files are test protocol state. Reformat the disk for every QEMU
# run so files left by an earlier run cannot be mistaken for new messages.
qemu_disk: FORCE
	$(SDDF)/tools/mkvirtdisk $@ 1 512 16777216 GPT

qemu: $(IMAGE_FILE) qemu_disk
	$(QEMU) -machine virt,virtualization=on \
		-cpu cortex-a53 \
		-serial mon:stdio \
		-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
		-m size=2G \
		-nographic \
		-global virtio-mmio.force-legacy=false \
		-drive file=qemu_disk,if=none,format=raw,id=hd \
		-device virtio-blk-device,drive=hd,bus=virtio-mmio-bus.1

FORCE:
