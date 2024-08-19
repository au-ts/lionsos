#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

TOOLCHAIN := clang
CC := clang
LD := ld.lld
RANLIB := llvm-ranlib
AR := llvm-ar
TARGET := aarch64-none-elf
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
DTC := dtc

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
PLATFORM := meson
SDDF := $(LIONSOS)/dep/sddf
LIBVMM_DIR := $(LIONSOS)/dep/libvmm

VMM_IMAGE_DIR := ${FILEIO_DIR}/src/vmm/images
LINUX := 90c4247bcd24cbca1a3db4b7489a835ce87a486e-linux
INITRD := 08c10529dc2806559d5c4b7175686a8206e10494-rootfs.cpio.gz
DTS := $(VMM_IMAGE_DIR)/linux.dts
DTB := linux.dtb

LWIP := $(SDDF)/network/ipstacks/lwip/src
FATFS := $(LIONSOS)/components/fs/fat
MUSL := $(LIONSOS)/dep/musllibc

IMAGES := timer_driver.elf \
	  eth_driver.elf \
	  micropython.elf \
	  fatfs.elf \
	  copy.elf \
	  network_virt_rx.elf \
	  network_virt_tx.elf \
	  uart_driver.elf \
	  serial_virt_rx.elf \
	  serial_virt_tx.elf

CFLAGS := \
	-mtune=$(CPU) \
	-mstrict-align \
	-ffreestanding \
	-g \
	-O0 \
	-Wall \
	-Wno-unused-function \
	-I$(BOARD_DIR)/include \
	-target $(TARGET) \
	-DBOARD_$(MICROKIT_BOARD) \
	-I$(SDDF)/include \
	-I${CONFIG_INCLUDE}

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a

IMAGE_FILE := file.img
REPORT_FILE := report.txt

all: cache.o
CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@


%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

include ${SDDF}/util/util.mk
include ${LIBVMM_DIR}/vmm.mk
include ${SDDF}/drivers/clock/${PLATFORM}/timer_driver.mk
include ${SDDF}/drivers/network/${PLATFORM}/eth_driver.mk
include ${SDDF}/drivers/serial/${PLATFORM}/uart_driver.mk
include ${SDDF}/network/components/network_components.mk
include ${SDDF}/serial/components/serial_components.mk
include ${SDDF}/libco/libco.mk

# Build the VMM for graphics
VMM_OBJS := vmm.o package_guest_images.o
VPATH := ${LIBVMM_DIR}:${VMM_IMAGE_DIR}:${VMM_IMAGE_DIR}/..

$(INITRD) $(LINUX):
	curl -L https://lionsos.org/downloads/examples/kitty/$@ -o $@

$(DTB): $(DTS)
	$(DTC) -q -I dts -O dtb $< > $@

package_guest_images.o: $(LIBVMM_DIR)/tools/package_guest_images.S \
			$(VMM_IMAGE_DIR) $(LINUX) $(INITRD) $(DTB)
	$(CC) -c -g3 -x assembler-with-cpp \
					-DGUEST_KERNEL_IMAGE_PATH=\"$(LINUX)\" \
					-DGUEST_DTB_IMAGE_PATH=\"$(DTB)\" \
					-DGUEST_INITRD_IMAGE_PATH=\"$(INITRD)\" \
					-target $(TARGET) \
					$< -o $@

vmm.elf: ${VMM_OBJS} libvmm.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

# Build with two threads in parallel
nproc=2

micropython.elf: mpy-cross libsddf_util_debug.a libco.a
	make  -C $(LIONSOS)/components/micropython -j$(nproc) \
			MICROKIT_SDK=$(MICROKIT_SDK) \
			MICROKIT_BOARD=$(MICROKIT_BOARD) \
			MICROKIT_CONFIG=$(MICROKIT_CONFIG) \
			MICROPY_MPYCROSS=$(abspath mpy_cross/mpy-cross) \
			MICROPY_MPYCROSS_DEPENDENCY=$(abspath mpy_cross/mpy-cross) \
			BUILD=$(abspath .) \
			LIBMATH=${LIBMATH} \
			CONFIG_INCLUDE=$(abspath $(CONFIG_INCLUDE)) \
			V=1

fatfs.elf: musllibc/lib/libc.a
	make -C $(LIONSOS)/components/fs/fatfs \
	       CC=$(CC) \
		   LD=$(LD) \
		   CPU=$(CPU) \
		   BUILD_DIR=$(abspath .) \
		   CONFIG=$(MICROKIT_CONFIG) \
	       MICROKIT_SDK=$(MICROKIT_SDK) \
		   MICROKIT_BOARD=$(MICROKIT_BOARD) \
		   LIBC_DIR=$(abspath $(BUILD_DIR)/musllibc) \
		   BUILD_DIR=$(abspath .) \
		   TARGET=$(target) \
		   USE_LIBMICROKITCO=1
		   
musllibc/lib/libc.a:
	make -C $(MUSL) \
		C_COMPILER=aarch64-none-elf-gcc \
		TOOLPREFIX=aarch64-none-elf- \
		CONFIG_ARCH_AARCH64=y \
		STAGE_DIR=$(abspath $(BUILD_DIR)/musllibc) \
		SOURCE_DIR=.

${IMAGES}: libsddf_util_debug.a

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) ${FILEIO_DIR}/fileio.system
	$(MICROKIT_TOOL) ${FILEIO_DIR}/fileio.system --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

FORCE:

%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

mpy-cross: FORCE
	${MAKE} -C ${LIONSOS}/dep/micropython/mpy-cross BUILD=$(abspath ./mpy_cross)
