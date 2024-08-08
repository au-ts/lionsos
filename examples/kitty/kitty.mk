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

VMM_IMAGE_DIR := ${KITTY_DIR}/src/vmm/images
LINUX := linux
INITRD := rootfs.cpio.gz
DTS := $(VMM_IMAGE_DIR)/linux.dts
DTB := linux.dtb

LWIP := $(SDDF)/network/ipstacks/lwip/src
LIBNFS := $(LIONSOS)/dep/libnfs
NFS := $(LIONSOS)/components/fs/nfs
MUSL := $(LIONSOS)/dep/musllibc
LIONSOS_DOWNLOADS := https://lionsos.org/downloads/examples/kitty
# I2C config
I2C_BUS_NUM=2

IMAGES := timer_driver.elf \
	  vmm.elf \
	  eth_driver.elf \
	  micropython.elf \
	  nfs.elf \
	  copy.elf \
	  network_virt_rx.elf \
	  network_virt_tx.elf \
	  uart_driver.elf \
	  serial_virt_rx.elf \
	  serial_virt_tx.elf \
	  i2c_virt.elf \
	  i2c_driver.elf

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

IMAGE_FILE := kitty.img
REPORT_FILE := report.txt

all: cache.o
CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@


%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

SDDF_MAKEFILES := ${SDDF}/util/util.mk \
	${SDDF}/drivers/clock/${PLATFORM}/timer_driver.mk \
	${SDDF}/drivers/network/${PLATFORM}/eth_driver.mk \
	${SDDF}/drivers/i2c/${PLATFORM}/i2c_driver.mk \
	${SDDF}/drivers/serial/${PLATFORM}/uart_driver.mk \
	${SDDF}/network/components/network_components.mk \
	${SDDF}/serial/components/serial_components.mk \
	${SDDF}/i2c/components/i2c_virt.mk \
	${SDDF}/libco/libco.mk

include ${SDDF_MAKEFILES}
include ${LIBVMM_DIR}/vmm.mk

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

micropython.elf: mpy-cross libsddf_util_debug.a libco.a
	make  -C $(LIONSOS)/components/micropython -j$(nproc) \
			MICROKIT_SDK=$(MICROKIT_SDK) \
			MICROKIT_BOARD=$(MICROKIT_BOARD) \
			MICROKIT_CONFIG=$(MICROKIT_CONFIG) \
			MICROPY_MPYCROSS=$(abspath mpy_cross/mpy-cross) \
			MICROPY_MPYCROSS_DEPENDENCY=$(abspath mpy_cross/mpy-cross) \
			BUILD=$(abspath .) \
			LIBMATH=${LIBMATH} \
			FROZEN_MANIFEST=${KITTY_DIR}/manifest.py \
			CONFIG_INCLUDE=$(abspath $(CONFIG_INCLUDE)) \
			ENABLE_I2C=1 \
			ENABLE_FRAMEBUFFER=1 \
			V=1

musllibc/lib/libc.a:
	make -C $(MUSL) \
		C_COMPILER=aarch64-none-elf-gcc \
		TOOLPREFIX=aarch64-none-elf- \
		CONFIG_ARCH_AARCH64=y \
		STAGE_DIR=$(abspath $(BUILD_DIR)/musllibc) \
		SOURCE_DIR=.

libnfs/lib/libnfs.a: musllibc/lib/libc.a
	MUSL=$(abspath musllibc) cmake -S $(LIBNFS) -B libnfs
	cmake --build libnfs

nfs/nfs.a: musllibc/lib/libc.a FORCE |${LIBNFS}/nfs
	make -C $(NFS) \
		BUILD_DIR=$(abspath $(BUILD_DIR)/nfs) \
		MICROKIT_INCLUDE=$(BOARD_DIR)/include \
		MUSLLIBC_INCLUDE=$(abspath musllibc/include) \
		LIBNFS_INCLUDE=$(abspath $(LIBNFS)/include) \
		CONFIG_INCLUDE=$(abspath $(CONFIG_INCLUDE))

nfs.elf: nfs/nfs.a libnfs/lib/libnfs.a musllibc/lib/libc.a
	$(LD) \
		$(LDFLAGS) \
		nfs/nfs.a \
		-Llibnfs/lib \
		-Lmusllibc/lib \
		-L$(LIBGCC) \
		-lgcc \
		-lc \
		$(LIBS) \
		-lnfs \
		-o nfs.elf

${IMAGES}: libsddf_util_debug.a

%.o: %.c ${SDDF}/include
	${CC} ${CFLAGS} -c -o $@ $<

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) ${KITTY_DIR}/kitty.system
	$(MICROKIT_TOOL) ${KITTY_DIR}/kitty.system --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

FORCE:

mpy-cross: FORCE ${LIONSOS}/dep/micropython/mpy-cross
	${MAKE} -C ${LIONSOS}/dep/micropython/mpy-cross BUILD=$(abspath ./mpy_cross)

# If you want to use your own VM for the graphics driver
# then change these lines or just make sure you've already put
# linux and rootfs.cpio.gz into the Build directory
${LINUX}:
	curl -L ${LIONSOS_DOWNLOADS}/$(KITTY_GRAPHICS_VM_LINUX) -o $@

${INITRD}:
	curl -L ${LIONSOS_DOWNLOADS}/$(KITTY_GRAPHICS_VM_ROOTFS) -o $@

# Because we include the *.mk files they are an implicit dependency
# for the build.  These rules instantiate the submodules that
# include the makefiles.
${LIBVMM_DIR}/vmm.mk:
	git submodule update --init $(LIONSOS)/dep/libvmm

${LIBNFS}/nfs:
	git submodule update --init $(LIONSOS)/dep/libnfs

$(LIONSOS)/dep/micropython/py/mkenv.mk ${LIONSOS}/dep/micropython/mpy-cross:	
	git submodule update --init $(LIONSOS)/dep/micropython
	cd ${LIONSOS}/dep/micropython && git submodule update --init lib/micropython-lib

	git submodule update --init $(LIONSOS)/dep/musllibc
	git submodule update --init $(LIONSOS)/dep/libmicrokitco

${SDDF_MAKEFILES} ${LIONSOS}/dep/sddf/include &: 
	git submodule update --init $(LIONSOS)/dep/sddf

