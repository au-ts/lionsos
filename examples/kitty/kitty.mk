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
AS := clang
PYTHON := python3

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
SDDF := $(LIONSOS)/dep/sddf
LIBVMM_DIR := $(LIONSOS)/dep/libvmm

ifeq ($(strip $(MICROKIT_BOARD)), odroidc4)
	NET_DRIV_DIR := meson
	UART_DRIV_DIR := meson
	TIMER_DRIV_DIR := meson
	I2C_DRIV_DIR := meson
	PINCTRL_DRIV_DIR := meson
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
NFS := $(LIONSOS)/components/fs/nfs
MUSL_SRC := $(LIONSOS)/dep/musllibc
MUSL := musllibc
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
	  i2c_driver.elf \

CFLAGS := \
	-mtune=$(CPU) \
	-mstrict-align \
	-ffreestanding \
	-g \
	-Wall \
	-Wno-unused-function \
	-I$(BOARD_DIR)/include \
	-target $(TARGET) \
	-DBOARD_$(MICROKIT_BOARD) \
	-I$(LIONSOS)/include \
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
	${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk \
	${SDDF}/drivers/network/${NET_DRIV_DIR}/eth_driver.mk \
	${SDDF}/drivers/serial/${UART_DRIV_DIR}/uart_driver.mk \
	${SDDF}/network/components/network_components.mk \
	${SDDF}/serial/components/serial_components.mk \
	${SDDF}/i2c/components/i2c_virt.mk \
	${SDDF}/libco/libco.mk

# We can build the kitty system without the I2C Driver
ifneq ($(I2C_DRIV_DIR), )
SDDF_MAKEFILES += ${SDDF}/drivers/i2c/${I2C_DRIV_DIR}/i2c_driver.mk
endif

ifneq ($(PINCTRL_DRIV_DIR), )
SDDF_MAKEFILES += ${SDDF}/drivers/pinctrl/${PINCTRL_DRIV_DIR}/pinctrl_driver.mk
ASFLAGS := -c -target ${TARGET}
DTS_FILE := ${KITTY_DIR}/board/$(MICROKIT_BOARD)/odroidc4_patched.dts
IMAGES += pinctrl_driver.elf 
SOC := hardkernel,odroid-c4
endif

include ${SDDF_MAKEFILES}
include ${LIBVMM_DIR}/vmm.mk
include ${NFS}/nfs.mk

ifneq ($(PINCTRL_DRIV_DIR), )
CFLAGS += ${CHIP_HEADER_INC}
endif

$(MUSL)/lib/libc.a $(MUSL)/include: ${MUSL}/Makefile
	make -C $(MUSL_SRC) \
		C_COMPILER=aarch64-none-elf-gcc \
		TOOLPREFIX=aarch64-none-elf- \
		CONFIG_ARCH_AARCH64=y \
		STAGE_DIR=$(abspath $(MUSL)) \
		SOURCE_DIR=.

# Build the VMM for graphics
VMM_OBJS := vmm.o package_guest_images.o
VPATH := ${LIBVMM_DIR}:${VMM_IMAGE_DIR}:${VMM_SRC_DIR}

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
		kitty.py pn532.py font.py writer.py \
		$(LIONSOS)/dep/libmicrokitco/Makefile
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
	cp $< $@

manifest.py: ${KITTY_DIR}/manifest.py
	cp $< $@

%.py: ${KITTY_DIR}/client/%.py
	cp $< $@

%.py: ${KITTY_DIR}/client/font/%.py
	cp $< $@

${IMAGES}: libsddf_util_debug.a

%.o: %.c ${SDDF}/include
	${CC} ${CFLAGS} -c -o $@ $<

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) ${KITTY_DIR}/board/${MICROKIT_BOARD}/kitty.system
	$(MICROKIT_TOOL) ${KITTY_DIR}/board/${MICROKIT_BOARD}/kitty.system --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

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
	cd ${LIONSOS}; git submodule update --init dep/libvmm

$(LIONSOS)/dep/micropython/py/mkenv.mk ${LIONSOS}/dep/micropython/mpy-cross:
	cd ${LIONSOS}; git submodule update --init dep/micropython
	cd ${LIONSOS}/dep/micropython && git submodule update --init lib/micropython-lib

${LIONSOS}/dep/libmicrokitco/Makefile:
	cd ${LIONSOS}; git submodule update --init dep/libmicrokitco

${MUSL}/Makefile:
	cd ${LIONSOS}; git submodule update --init dep/musllibc

${SDDF_MAKEFILES} ${LIONSOS}/dep/sddf/include &:
	cd ${LIONSOS}; git submodule update --init dep/sddf

qemu: $(IMAGE_FILE)
	$(QEMU) -machine virt,virtualization=on \
			-cpu cortex-a53 \
			-serial mon:stdio \
			-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
			-m size=2G \
			-device virtio-net-device,netdev=netdev0 \
			-netdev user,id=netdev0 \
			-global virtio-mmio.force-legacy=false \
			-device virtio-gpu-pci

clean::
	${RM} -f *.elf .depend* $
	find . -name \*.[do] |xargs --no-run-if-empty rm

clobber:: clean
	rm -f *.a
	rm -f ${IMAGE_FILE} ${REPORT_FILE}
