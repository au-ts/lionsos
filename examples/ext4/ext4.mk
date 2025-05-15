#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
IMAGES := \
	ext4.elf \
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

METAPROGRAM := $(EXT4_DIR)/meta.py
DTS := $(SDDF)/dts/$(MICROKIT_BOARD).dts
DTB := $(MICROKIT_BOARD).dtb

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

SYSTEM_FILE := ext4.system
IMAGE_FILE := loader.img
REPORT_FILE := report.txt

CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@


%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

BLK_DRIVER := $(SDDF)/drivers/blk/${BLK_DRIV_DIR}
BLK_COMPONENTS := $(SDDF)/blk/components

include ${SDDF}/util/util.mk
include ${SDDF}/libco/libco.mk
include ${BLK_DRIVER}/blk_driver.mk
include ${BLK_COMPONENTS}/blk_components.mk

${IMAGES}: libsddf_util_debug.a

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

FORCE:

%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

MUSL_SRC := $(LIONSOS)/dep/musllibc
MUSL := musllibc

LIBC_LIB := musllibc/lib/libc.a
LIBC_INCLUDE := musllibc/include

$(MUSL):
	mkdir -p $@

$(MUSL)/lib/libc.a $(MUSL)/include: ${MUSL}
	cd ${MUSL} && CC=aarch64-none-elf-gcc CROSS_COMPILE=aarch64-none-elf- ${MUSL_SRC}/configure --srcdir=${MUSL_SRC} --prefix=${abspath ${MUSL}} --target=aarch64 --with-malloc=oldmalloc --enable-warnings --disable-shared --enable-static
	${MAKE} -C ${MUSL} install

LWEXT4_FILES := $(LWEXT4)/src/ext4_bitmap.c \
				$(LWEXT4)/src/ext4_crc32.c \
				$(LWEXT4)/src/ext4_dir.c \
				$(LWEXT4)/src/ext4_hash.c \
				$(LWEXT4)/src/ext4_journal.c \
				$(LWEXT4)/src/ext4_super.c \
				$(LWEXT4)/src/ext4.c \
				$(LWEXT4)/src/ext4_balloc.c \
				$(LWEXT4)/src/ext4_block_group.c \
				$(LWEXT4)/src/ext4_debug.c \
				$(LWEXT4)/src/ext4_extent.c \
				$(LWEXT4)/src/ext4_ialloc.c \
				$(LWEXT4)/src/ext4_mbr.c \
				$(LWEXT4)/src/ext4_trans.c \
				$(LWEXT4)/src/ext4_bcache.c \
				$(LWEXT4)/src/ext4_blockdev.c \
				$(LWEXT4)/src/ext4_dir_idx.c \
				$(LWEXT4)/src/ext4_fs.c \
				$(LWEXT4)/src/ext4_inode.c \
				$(LWEXT4)/src/ext4_mkfs.c \
				$(LWEXT4)/src/ext4_xattr.c

%.o: $(LWEXT4)/src/%.c $(LIBC_INCLUDE)
	${CC} ${CFLAGS} -I$(LIBC_INCLUDE) -DVERSION="\"1.0.0\"" -DCONFIG_USE_DEFAULT_CFG -I$(LWEXT4)/include -c $(LWEXT4_FILES) $(EXT4_DIR)/ext4.c

ext4.o: $(EXT4_DIR)/ext4.c
	${CC} ${CFLAGS} -c -o $@ $<

ext4.elf: ext4_%.o $(LIBC_LIB) ext4.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS} $(LIBC_LIB)

$(DTB): $(DTS)
	$(DTC) -q -I dts -O dtb $(DTS) > $(DTB)

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE)
	$(OBJCOPY) --update-section .device_resources=blk_driver_device_resources.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_driver_config=blk_driver.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_virt_config=blk_virt.data blk_virt.elf
	$(OBJCOPY) --update-section .blk_client_config=blk_client_ext4.data ext4.elf

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
