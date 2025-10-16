#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
IMAGES := \
	timer_driver.elf \
	eth_driver.elf \
	doom.elf \
	fat.elf \
	dcss.elf \
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
LWIP := $(SDDF)/network/ipstacks/lwip/src
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco

ifeq ($(ARCH),aarch64)
	CFLAGS_ARCH := -mcpu=$(CPU)
	TARGET := aarch64-none-elf
else ifeq ($(ARCH),riscv64)
	CFLAGS_ARCH := -march=rv64imafdc
	TARGET := riscv64-none-elf
else
$(error Unsupported ARCH given)
endif

ifeq ($(strip $(TOOLCHAIN)), clang)
	CFLAGS_ARCH += -target $(TARGET)
endif

METAPROGRAM := $(DOOM_DIR)/meta.py
DTS := $(SDDF)/dts/$(MICROKIT_BOARD).dts
DTB := $(MICROKIT_BOARD).dtb

FAT := $(LIONSOS)/components/fs/fat

CFLAGS := \
	-mtune=$(CPU) \
	-mstrict-align \
	-ffreestanding \
	-Ofast \
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
	-I$(LIBMICROKITCO_PATH) \
	-I$(DOOM_DIR)/sel4devkit-maaxboard-microkit-hdmi-driver-main/include \
	-I$(DOOM_DIR) \
	-I$(DOOM_DIR)/doomgeneric/doomgeneric

include $(LIONSOS)/lib/libc/libc.mk

LDFLAGS := -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a -lc

SYSTEM_FILE := doom.system
IMAGE_FILE := doom.img
REPORT_FILE := report.txt

all: $(IMAGES)
CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@

%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

BLK_DRIVER := $(SDDF)/drivers/blk/${BLK_DRIV_DIR}
BLK_COMPONENTS := $(SDDF)/blk/components

SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include
include ${SDDF}/util/util.mk
include ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk
include ${SDDF}/drivers/serial/${SERIAL_DRIV_DIR}/serial_driver.mk
include ${SDDF}/serial/components/serial_components.mk
include ${SDDF}/network/lib_sddf_lwip/lib_sddf_lwip.mk
include ${SDDF}/libco/libco.mk
include ${BLK_DRIVER}/blk_driver.mk
include ${BLK_COMPONENTS}/blk_components.mk

FAT_LIBC_LIB := $(LIONS_LIBC)/lib/libc.a
FAT_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIONSOS)/components/fs/fat/fat.mk

LIBMICROKITCO_CFLAGS_doom = -I$(DOOM_DIR)/libmicrokitco_opts.h
LIBMICROKITCO_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIBMICROKITCO_PATH)/libmicrokitco.mk

${IMAGES}: $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

FORCE:

%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

# === Video driver ===
VENDORED_VIDEO_DRIVER_DIR = $(DOOM_DIR)/sel4devkit-maaxboard-microkit-hdmi-driver-main

DCSS_OBJS = dcss.o dma.o vic_table.o API_general.o test_base_sw.o util.o write_register.o API_AFE_t28hpc_hdmitx.o API_AFE.o vic_table.o API_HDMITX.o API_AVI.o API_Infoframe.o hdmi_tx.o context_loader.o dpr.o dtg.o scaler.o sub_sampler.o
DCSS_INC := $(VENDORED_VIDEO_DRIVER_DIR)/include $(VENDORED_VIDEO_DRIVER_DIR)/include/hdmi $(VENDORED_VIDEO_DRIVER_DIR)/include/dcss $(VENDORED_VIDEO_DRIVER_DIR)/include/util
DCSS_INC_FLAGS=$(foreach d, $(DCSS_INC), -I$d)

%.o: $(VENDORED_VIDEO_DRIVER_DIR)/src/hdmi/%.c
	${CC} ${CFLAGS} ${DCSS_INC_FLAGS} -c -o $@ $<

%.o: $(VENDORED_VIDEO_DRIVER_DIR)/src/dcss/%.c
	${CC} ${CFLAGS} ${DCSS_INC_FLAGS} -c -o $@ $<

%.o: $(VENDORED_VIDEO_DRIVER_DIR)/src/util/%.c
	${CC} ${CFLAGS}${DCSS_INC_FLAGS} -c -o $@ $<

frame_buffer.o: $(VENDORED_VIDEO_DRIVER_DIR)/src/api/frame_buffer.c
	${CC} ${CFLAGS} -I$(VENDORED_VIDEO_DRIVER_DIR)/include -c -o $@ $<

dcss.elf: $(DCSS_OBJS)
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

# ====================

DOOMGENERIC_DIR := $(DOOM_DIR)/doomgeneric/doomgeneric

DOOM_OBJS = dummy.o am_map.o doomdef.o doomstat.o dstrings.o d_event.o d_items.o d_iwad.o d_loop.o d_main.o d_mode.o d_net.o f_finale.o f_wipe.o g_game.o hu_lib.o hu_stuff.o info.o i_cdmus.o i_endoom.o i_joystick.o i_scale.o i_sound.o i_system.o i_timer.o memio.o m_argv.o m_bbox.o m_cheat.o m_config.o m_controls.o m_fixed.o m_menu.o m_misc.o m_random.o p_ceilng.o p_doors.o p_enemy.o p_floor.o p_inter.o p_lights.o p_map.o p_maputl.o p_mobj.o p_plats.o p_pspr.o p_saveg.o p_setup.o p_sight.o p_spec.o p_switch.o p_telept.o p_tick.o p_user.o r_bsp.o r_data.o r_draw.o r_main.o r_plane.o r_segs.o r_sky.o r_things.o sha1.o sounds.o statdump.o st_lib.o st_stuff.o s_sound.o tables.o v_video.o wi_stuff.o w_checksum.o w_file.o w_main.o w_wad.o z_zone.o w_file_stdc.o i_input.o i_video.o doomgeneric.o

doom.o: $(DOOM_DIR)/doom.c
	${CC} ${CFLAGS} -c -o $@ $<

%.o: $(DOOMGENERIC_DIR)/%.c
	${CC} ${CFLAGS} -c -o $@ $<

doom.elf: $(DOOM_OBJS) doom.o libmicrokitco_doom.a frame_buffer.o vic_table.o
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

$(DTB): $(DTS)
	$(DTC) -q -I dts -O dtb $(DTS) > $(DTB)

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE)
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .serial_virt_rx_config=serial_virt_rx.data serial_virt_rx.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .device_resources=blk_driver_device_resources.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_driver_config=blk_driver.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_virt_config=blk_virt.data blk_virt.elf
	$(OBJCOPY) --update-section .blk_client_config=blk_client_fatfs.data fat.elf
	$(OBJCOPY) --update-section .fs_server_config=fs_server_fatfs.data fat.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_doom.data doom.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_doom.data doom.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_doom.data doom.elf

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

qemu_disk:
	$(LIONSOS)/dep/sddf/tools/mkvirtdisk $@ 1 512 16777216 GPT

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
