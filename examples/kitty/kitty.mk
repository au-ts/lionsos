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
SDDF := $(LIONSOS)/dep/sddf
LIBVMM_DIR := $(LIONSOS)/dep/libvmm

ifeq ($(strip $(MICROKIT_BOARD)), odroidc4)
	NET_DRIV_DIR := meson
	UART_DRIV_DIR := meson
	TIMER_DRIV_DIR := meson
	I2C_DRIV_DIR := meson
	CPU := cortex-a55
	INITRD := 08c10529dc2806559d5c4b7175686a8206e10494-rootfs.cpio.gz
	LINUX := 90c4247bcd24cbca1a3db4b7489a835ce87a486e-linux
else ifeq ($(strip $(MICROKIT_BOARD)), qemu_virt_aarch64)
	NET_DRIV_DIR := virtio
	UART_DRIV_DIR := arm
	TIMER_DRIV_DIR := arm
	CPU := cortex-a53
	QEMU := qemu-system-aarch64
	INITRD := 8d4a14e2c92a638d68f04832580a57b94e8a4f6b-rootfs.cpio.gz
	LINUX := 75757e2972d5dc528d98cab377e2c74a9e02d6f9-linux
else
$(error Unsupported MICROKIT_BOARD given)
endif

VMM_IMAGE_DIR := ${KITTY_DIR}/board/$(MICROKIT_BOARD)/framebuffer_vmm_images
VMM_SRC_DIR := ${KITTY_DIR}/src/vmm
DTS := $(VMM_IMAGE_DIR)/linux.dts
DTB := linux.dtb

LWIP := $(SDDF)/network/ipstacks/lwip/src
LIBNFS := $(LIONSOS)/dep/libnfs
NFS := $(LIONSOS)/components/fs/nfs
MUSL := $(LIONSOS)/dep/musllibc

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

include ${SDDF}/util/util.mk
include ${LIBVMM_DIR}/vmm.mk
include ${SDDF}/drivers/clock/${TIMER_DRIV_DIR}/timer_driver.mk
include ${SDDF}/drivers/network/${NET_DRIV_DIR}/eth_driver.mk

# We can build the kitty system without the I2C Driver
ifneq ($(I2C_DRIV_DIR), )
include ${SDDF}/drivers/i2c/${I2C_DRIV_DIR}/i2c_driver.mk
endif

include ${SDDF}/drivers/serial/${UART_DRIV_DIR}/uart_driver.mk
include ${SDDF}/network/components/network_components.mk
include ${SDDF}/serial/components/serial_components.mk
include ${SDDF}/i2c/components/i2c_virt.mk
include ${SDDF}/libco/libco.mk

# Build the VMM for graphics
VMM_OBJS := vmm.o package_guest_images.o
VPATH := ${LIBVMM_DIR}:${VMM_IMAGE_DIR}:${VMM_SRC_DIR}

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

micropython.elf: mpy-cross libsddf_util_debug.a libco.a config.py manifest.py \
				client/kitty.py client/pn532.py font/font.py font/writer.py
	make  -C $(LIONSOS)/components/micropython -j$(nproc) \
			MICROKIT_SDK=$(MICROKIT_SDK) \
			MICROKIT_BOARD=$(MICROKIT_BOARD) \
			MICROKIT_CONFIG=$(MICROKIT_CONFIG) \
			MICROPY_MPYCROSS=$(abspath mpy_cross/mpy-cross) \
			MICROPY_MPYCROSS_DEPENDENCY=$(abspath mpy_cross/mpy-cross) \
			BUILD=$(abspath $(BUILD_DIR)) \
			LIBMATH=${LIBMATH} \
			FROZEN_MANIFEST=$(abspath manifest.py) \
			CONFIG_INCLUDE=$(abspath $(CONFIG_INCLUDE)) \
			ENABLE_I2C=1 \
			ENABLE_FRAMEBUFFER=1 \
			V=1

config.py: ${KITTY_DIR}/board/$(MICROKIT_BOARD)/config.py
	cp $< .

manifest.py: ${KITTY_DIR}/manifest.py
	cp ${KITTY_DIR}/manifest.py .

client/%.py: ${KITTY_DIR}/client/%.py
	cp $< .

font/%.py: ${KITTY_DIR}/client/font/%.py
	cp $< .

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

nfs/nfs.a: musllibc/lib/libc.a FORCE
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

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) ${KITTY_DIR}/board/${MICROKIT_BOARD}/kitty.system
	$(MICROKIT_TOOL) ${KITTY_DIR}/board/${MICROKIT_BOARD}/kitty.system --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

FORCE:

%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

mpy-cross: FORCE
	${MAKE} -C ${LIONSOS}/dep/micropython/mpy-cross BUILD=$(abspath ./mpy_cross)

qemu: $(IMAGE_FILE)
	$(QEMU) -machine virt,virtualization=on \
			-cpu cortex-a53 \
			-serial mon:stdio \
			-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
			-m size=2G \
			-device virtio-net-device,netdev=netdev0 \
			-netdev user,id=netdev0 \
			-global virtio-mmio.force-legacy=false \
			-d guest_errors \
			-device virtio-gpu-pci

clean::
	${RM} -f *.elf .depend* $
	find . -name \*.[do] |xargs --no-run-if-empty rm

clobber:: clean
	rm -f *.a
	rm -f ${IMAGE_FILE} ${REPORT_FILE}