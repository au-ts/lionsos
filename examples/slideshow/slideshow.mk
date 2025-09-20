#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
IMAGES := \
	timer_driver.elf \
	fat.elf \
	serial_driver.elf \
	serial_virt_rx.elf \
	serial_virt_tx.elf \
	blk_virt.elf \
	blk_driver.elf \
	slideshow.elf \
 	dcss.elf

ifeq ($(strip $(MICROKIT_BOARD)), maaxboard)
	BLK_DRIV_DIR := mmc/imx
	SERIAL_DRIV_DIR := imx
	TIMER_DRIV_DIR := imx
	CPU := cortex-a53
else
$(error Unsupported MICROKIT_BOARD given)
endif

TOOLCHAIN := clang
CC := clang
LD := ld.lld
RANLIB := llvm-ranlib
AR := llvm-ar
OBJCOPY := llvm-objcopy
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
DTC := dtc
PYTHON ?= python3

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
ARCH := $(shell grep 'CONFIG_SEL4_ARCH  ' $(BOARD_DIR)/include/kernel/gen_config.h | cut -d' ' -f4)
SDDF := $(LIONSOS)/dep/sddf

ifeq ($(ARCH),aarch64)
	CFLAGS_ARCH := -mcpu=$(CPU)
	TARGET := aarch64-none-elf
else
$(error Unsupported ARCH given)
endif

ifeq ($(strip $(TOOLCHAIN)), clang)
	CFLAGS_ARCH += -target $(TARGET)
endif

METAPROGRAM := $(SLIDESHOW_DIR)/meta.py
DTS := $(SDDF)/dts/$(MICROKIT_BOARD).dts
DTB := $(MICROKIT_BOARD).dtb

FAT := $(LIONSOS)/components/fs/fat
MUSL_SRC := $(LIONSOS)/dep/musllibc
MUSL := musllibc

CFLAGS := \
	-mtune=$(CPU) \
	-mstrict-align \
	-ffreestanding \
	-O2 \
	-g3 \
	-Wall \
	-Wno-unused-function \
	-Wno-bitwise-op-parentheses \
	-Wno-shift-op-parentheses \
	-I$(BOARD_DIR)/include \
	$(CFLAGS_ARCH) \
	-DBOARD_$(MICROKIT_BOARD) \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(MUSL)/include

LDFLAGS := -L$(BOARD_DIR)/lib -L$(MUSL)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a -lc

SYSTEM_FILE := slideshow.system
IMAGE_FILE := slideshow.img
REPORT_FILE := report.txt

all: cache.o
CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@

BLK_DRIVER := $(SDDF)/drivers/blk/${BLK_DRIV_DIR}
BLK_COMPONENTS := $(SDDF)/blk/components

include ${SDDF}/util/util.mk
include ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk
include ${SDDF}/drivers/serial/${SERIAL_DRIV_DIR}/serial_driver.mk
include ${SDDF}/serial/components/serial_components.mk
include ${BLK_DRIVER}/blk_driver.mk
include ${BLK_COMPONENTS}/blk_components.mk

FAT_LIBC_LIB := musllibc/lib/libc.a
FAT_LIBC_INCLUDE := musllibc/include
include $(LIONSOS)/components/fs/fat/fat.mk

LIB_COMPILER_RT_LIBC_INCLUDE := $(MUSL)/include
include $(LIONSOS)/lib/compiler_rt/lib_compiler_rt.mk

$(MUSL):
	mkdir -p $@

$(MUSL)/lib/libc.a $(MUSL)/include: ${MUSL_SRC}/Makefile ${MUSL}
	cd ${MUSL} && CC=$(CC) CFLAGS="-target $(TARGET) -mtune=$(CPU)" ${MUSL_SRC}/configure CROSS_COMPILE=llvm- --srcdir=${MUSL_SRC} --prefix=${abspath ${MUSL}} --target=$(TARGET) --with-malloc=oldmalloc --enable-warnings --disable-shared --enable-static
	${MAKE} -C ${MUSL} install -j12

${IMAGES}: libsddf_util_debug.a

# === Video driver ===
VENDORED_VIDEO_DRIVER_DIR = $(SLIDESHOW_DIR)/sel4devkit-maaxboard-microkit-hdmi-driver-main

DCSS_OBJS = dcss.o dma.o vic_table.o API_general.o test_base_sw.o util.o write_register.o API_AFE_t28hpc_hdmitx.o API_AFE.o vic_table.o API_HDMITX.o API_AVI.o API_Infoframe.o hdmi_tx.o context_loader.o dpr.o dtg.o scaler.o sub_sampler.o
DCSS_INC := $(VENDORED_VIDEO_DRIVER_DIR)/include $(VENDORED_VIDEO_DRIVER_DIR)/include/hdmi $(VENDORED_VIDEO_DRIVER_DIR)/include/dcss $(VENDORED_VIDEO_DRIVER_DIR)/include/util
DCSS_INC_FLAGS=$(foreach d, $(DCSS_INC), -I$d)

%.o: $(VENDORED_VIDEO_DRIVER_DIR)/src/hdmi/%.c
	${CC} ${CFLAGS} ${DCSS_INC_FLAGS} -c -o $@ $<

%.o: $(VENDORED_VIDEO_DRIVER_DIR)/src/dcss/%.c
	${CC} ${CFLAGS} ${DCSS_INC_FLAGS} -c -o $@ $<

%.o: $(VENDORED_VIDEO_DRIVER_DIR)/src/util/%.c
	${CC} ${CFLAGS}${DCSS_INC_FLAGS} -c -o $@ $<

dcss.elf: $(DCSS_OBJS) lib_compiler_rt.a
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

# ====================

# === Slideshow PD ===
LLVM=1
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco
LIBMICROKITCO_OPT_PATH := $(SLIDESHOW_DIR)
LIBMICROKITCO_OBJ := $(BUILD_DIR)/libmicrokitco/libmicrokitco.a

export LIBMICROKITCO_PATH TARGET MICROKIT_SDK BUILD_DIR MICROKIT_BOARD MICROKIT_CONFIG CPU LLVM
$(LIBMICROKITCO_OBJ):
	make -f $(LIBMICROKITCO_PATH)/Makefile LIBMICROKITCO_OPT_PATH=$(LIBMICROKITCO_OPT_PATH)

fs_blocking_calls.o: $(SLIDESHOW_DIR)/fs_blocking_calls.c
	${CC} ${CFLAGS} -I${LIBMICROKITCO_PATH} -I${LIBMICROKITCO_OPT_PATH} -c -o $@ $<

fs_client_helpers.o: $(SLIDESHOW_DIR)/fs_client_helpers.c
	${CC} ${CFLAGS} -I${LIBMICROKITCO_PATH} -I${LIBMICROKITCO_OPT_PATH} -c -o $@ $<

slideshow.o: $(SLIDESHOW_DIR)/slideshow.c
	${CC} ${CFLAGS} -I${LIBMICROKITCO_PATH} -I${LIBMICROKITCO_OPT_PATH} -c -o $@ $<

slideshow.elf: slideshow.o fs_blocking_calls.o fs_client_helpers.o $(LIBMICROKITCO_OBJ) lib_compiler_rt.a
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}
# =====================

$(DTB): $(DTS)
	$(DTC) -q -I dts -O dtb $(DTS) > $(DTB)

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE)
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .serial_virt_rx_config=serial_virt_rx.data serial_virt_rx.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_slideshow.data slideshow.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_slideshow.data slideshow.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_slideshow.data slideshow.elf
	$(OBJCOPY) --update-section .device_resources=blk_driver_device_resources.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_driver_config=blk_driver.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_virt_config=blk_virt.data blk_virt.elf
	$(OBJCOPY) --update-section .blk_client_config=blk_client_fatfs.data fat.elf
	$(OBJCOPY) --update-section .fs_server_config=fs_server_fatfs.data fat.elf

$(IMAGE_FILE) $(REPORT_FILE): $(MUSL)/include $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

