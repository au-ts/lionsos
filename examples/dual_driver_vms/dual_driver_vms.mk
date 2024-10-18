#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
QEMU := qemu-system-aarch64

MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
SYSTEM_DIR := $(EXAMPLE_DIR)/board/$(MICROKIT_BOARD)
FATFS := $(LIONSOS)/components/fs/fat
MUSL := $(LIONSOS)/dep/musllibc

# vmm1
VM_ETH_DIR := $(SYSTEM_DIR)/vm_eth

NET_DRIVER_VM_USERLEVEL := uio_net_driver
NET_DRIVER_VM_USERLEVEL_INIT := net_driver_init

# vmm2
VM_SDMMC_DIR := $(SYSTEM_DIR)/vm_sdmmc

SYSTEM_FILE := $(SYSTEM_DIR)/dual_driver_vms.system
IMAGE_FILE := loader.img
REPORT_FILE := report.txt

vpath %.c $(LIBVMM) $(EXAMPLE_DIR)

CFLAGS := \
	  -mstrict-align \
	  -ffreestanding \
	  -g3 -O3 -Wall \
	  -Wno-unused-function \
	  -DMICROKIT_CONFIG_$(MICROKIT_CONFIG) \
	  -DBOARD_$(MICROKIT_BOARD) \
	  -I$(BOARD_DIR)/include \
	  -I$(LIBVMM)/include \
	  -I$(SDDF)/include \
	  -I$(EXAMPLE_DIR)/include \
    -I${CONFIG_INCLUDE} \
	  -MD \
	  -MP \
	  -target $(TARGET)

CFLAGS_USERLEVEL := \
		-g3 \
		-O3 \
		-Wno-unused-command-line-argument \
		-Wall -Wno-unused-function \
		-D_GNU_SOURCE \
		-target aarch64-linux-gnu \
		-I$(EXAMPLE_DIR) \
		-I$(BOARD_DIR)/include \
		-I$(VIRTIO_EXAMPLE)/include \
		-I$(SDDF)/include

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := --start-group -lmicrokit -Tmicrokit.ld libsddf_util_debug.a libvmm.a --end-group

CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- $(CFLAGS) $(BOARD) $(MICROKIT_CONFIG) | shasum | sed 's/ *-//')

$(CHECK_FLAGS_BOARD_MD5):
	-rm -f .board_cflags-*
	touch $@

UART_DRIVER := $(SDDF)/drivers/serial/meson
SERIAL_COMPONENTS := $(SDDF)/serial/components
BLK_COMPONENTS := $(SDDF)/blk/components
include $(UART_DRIVER)/uart_driver.mk
include $(SERIAL_COMPONENTS)/serial_components.mk
include $(BLK_COMPONENTS)/blk_components.mk
include $(LIBVMM)/vmm.mk
include ${SDDF}/libco/libco.mk
include $(LIBVMM_TOOLS)/linux/uio/uio.mk
include $(LIBVMM_TOOLS)/linux/net/net_init.mk
include $(LIBVMM_TOOLS)/linux/uio_drivers/net/uio_net.mk
include $(LIBVMM_TOOLS)/linux/blk/blk_init.mk
include $(LIBVMM_TOOLS)/linux/uio_drivers/blk/uio_blk.mk

CLIENT_VM_USERLEVEL_INIT := blk_client_init
BLK_DRIVER_VM_USERLEVEL := uio_blk_driver
BLK_DRIVER_VM_USERLEVEL_INIT := blk_driver_init

micropython.elf: mpy-cross libsddf_util_debug.a libco.a
	cp $(LIONSOS)/examples/dual_driver_vms/test.py .
	cp $(LIONSOS)/examples/dual_driver_vms/manifest.py .
	make  -C $(LIONSOS)/components/micropython -j$(nproc) \
		MICROKIT_SDK=$(MICROKIT_SDK) \
		MICROKIT_BOARD=$(MICROKIT_BOARD) \
		MICROKIT_CONFIG=$(MICROKIT_CONFIG) \
		MICROPY_MPYCROSS=$(abspath mpy_cross/mpy-cross) \
		MICROPY_MPYCROSS_DEPENDENCY=$(abspath mpy_cross/mpy-cross) \
		BUILD=$(abspath .) \
		LIBMATH=${LIBMATH} \
		CONFIG_INCLUDE=$(abspath $(CONFIG_INCLUDE)) \
		FROZEN_MANIFEST=$(abspath ./manifest.py) \
		V=1

fat.elf: musllibc/lib/libc.a
	make -C $(LIONSOS)/components/fs/fat \
		CC=$(CC) \
		LD=$(LD) \
		CPU=$(CPU) \
		BUILD_DIR=$(abspath .) \
		CONFIG=$(MICROKIT_CONFIG) \
		MICROKIT_SDK=$(MICROKIT_SDK) \
		MICROKIT_BOARD=$(MICROKIT_BOARD) \
		LIBC_DIR=$(abspath $(BUILD_DIR)/musllibc) \
		BUILD_DIR=$(abspath .) \
		EXAMPLE_SRC_DIR=$(abspath $(LIONSOS)/examples/fileio/src) \
		TARGET=$(TARGET)

musllibc/lib/libc.a:
	make -C $(MUSL) \
		C_COMPILER=aarch64-none-elf-gcc \
		TOOLPREFIX=aarch64-none-elf- \
		CONFIG_ARCH_AARCH64=y \
		STAGE_DIR=$(abspath $(BUILD_DIR)/musllibc) \
		SOURCE_DIR=.

%_vm:
	mkdir -p $@

IMAGES = vmm1.elf timer_driver.elf clk_driver.elf pinctrl_driver.elf fatfs.elf \
					uart_driver.elf $(SERIAL_IMAGES) blk_driver_vmm.elf $(BLK_IMAGES) micropython.elf

all: loader.img

-include vmm.d

$(IMAGES): libvmm.a libsddf_util_debug.a

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

# Ethernet VM
vmm1.elf: vmm1.o images1.o
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

rootfs1.cpio.gz: $(VM_ETH_DIR)/rootfs.cpio.gz $(NET_DRIVER_VM_USERLEVEL) $(NET_DRIVER_VM_USERLEVEL_INIT)
	$(LIBVMM)/tools/packrootfs $(VM_ETH_DIR)/rootfs.cpio.gz rootfs1 -o $@ \
		--startup $(NET_DRIVER_VM_USERLEVEL_INIT) \
		--home $(NET_DRIVER_VM_USERLEVEL)

vm1.dts: $(VM_ETH_DIR)/linux.dts $(VM_ETH_DIR)/overlay.dts
	$(LIBVMM)/tools/dtscat $^ > $@

vm1.dtb: vm1.dts
	$(DTC) -q -I dts -O dtb $< > $@

vmm1.o: $(VM_ETH_DIR)/vmm1.c $(CHECK_FLAGS_BOARD_MD5)
	$(CC) $(CFLAGS) -DVMM_MACHINE_NAME="\"Ethernet driver VM\"" -c -o $@ $<

images1.o: $(LIBVMM)/tools/package_guest_images.S $(VM_ETH_DIR)/linux vm1.dtb rootfs1.cpio.gz
	$(CC) -c -g3 -x assembler-with-cpp \
					-DGUEST_KERNEL_IMAGE_PATH=\"$(VM_ETH_DIR)/linux\" \
					-DGUEST_DTB_IMAGE_PATH=\"vm1.dtb\" \
					-DGUEST_INITRD_IMAGE_PATH=\"rootfs1.cpio.gz\" \
					-target $(TARGET) \
					$(LIBVMM)/tools/package_guest_images.S -o $@

# SDMMC VM
%_vm/vm.dts: $(SYSTEM_DIR)/%_vm/dts/linux.dts \
	$(SYSTEM_DIR)/%_vm/dts/overlays/*.dts $(CHECK_FLAGS_BOARD_MD5) |%_vm
	$(LIBVMM)/tools/dtscat $^ > $@

%_vm/vm.dtb: %_vm/vm.dts |%_vm
	$(DTC) -q -I dts -O dtb $< > $@

%_vmm.elf: %_vm/vmm.o %_vm/images.o
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

%_vm/vmm.o: $(VIRTIO_EXAMPLE)/%_vmm.c $(CHECK_FLAGS_BOARD_MD5) |%_vm
	$(CC) $(CFLAGS) -c -DVMM_MACHINE_NAME="\"$(%)\"" -o $@ $<

blk_driver_vm/rootfs.cpio.gz: $(SYSTEM_DIR)/blk_driver_vm/rootfs.cpio.gz \
	$(BLK_DRIVER_VM_USERLEVEL) $(BLK_DRIVER_VM_USERLEVEL_INIT) |blk_driver_vm
	$(LIBVMM)/tools/packrootfs $(SYSTEM_DIR)/blk_driver_vm/rootfs.cpio.gz \
		blk_driver_vm/rootfs -o $@ \
		--startup $(BLK_DRIVER_VM_USERLEVEL_INIT) \
		--home $(BLK_DRIVER_VM_USERLEVEL)

blk_storage:
	$(LIBVMM_TOOLS)/mkvirtdisk $@ $(BLK_NUM_PART) $(BLK_SIZE) $(BLK_MEM)

%_vm/images.o: $(LIBVMM)/tools/package_guest_images.S $(CHECK_FLAGS_BOARD_MD5) \
	$(SYSTEM_DIR)/%_vm/linux %_vm/vm.dtb %_vm/rootfs.cpio.gz
	$(CC) -c -g3 -x assembler-with-cpp \
					-DGUEST_KERNEL_IMAGE_PATH=\"$(SYSTEM_DIR)/$(@D)/linux\" \
					-DGUEST_DTB_IMAGE_PATH=\"$(@D)/vm.dtb\" \
					-DGUEST_INITRD_IMAGE_PATH=\"$(@D)/rootfs.cpio.gz\" \
					-target $(TARGET) \
					$(LIBVMM)/tools/package_guest_images.S -o $@

CLK_DRIVER := $(SDDF)/drivers/clk/meson
TIMER_DRIVER := $(SDDF)/drivers/timer/meson
PINCTRL_DRIVER := $(SDDF)/drivers/pinctrl/meson

export CLK_DRIVER_DIR := ${SDDF}/drivers/clk/meson
export DTS_FILE := $(EXAMPLE_DIR)/board/$(MICROKIT_BOARD)/odroidc4_patched.dts
export PYTHON := python3
export SOC := hardkernel,odroid-c4
export ASFLAGS := -c -target ${TARGET}
# bit of a hack for now
AS := clang 

include ${SDDF}/util/util.mk
include $(LIBVMM)/vmm.mk
include ${TIMER_DRIVER}/timer_driver.mk
include ${CLK_DRIVER}/clk_driver.mk
include ${PINCTRL_DRIVER}/pinctrl_driver.mk

FORCE:

mpy-cross: FORCE
	${MAKE} -C ${LIONSOS}/dep/micropython/mpy-cross BUILD=$(abspath ./mpy_cross)

clean::
	$(RM) -f *.elf .depend* $
	find . -name \*.[do] |xargs --no-run-if-empty rm

clobber:: clean
	rm -f *.a
	rm -f $(IMAGE_FILE) $(REPORT_FILE)
