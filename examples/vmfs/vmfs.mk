#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
IMAGES := \
	timer_driver.elf \
	eth_driver.elf \
	micropython.elf \
	fs_driver_vmm.elf \
	network_copy.elf \
	network_virt_rx.elf \
	network_virt_tx.elf \
	uart_driver.elf \
	serial_virt_rx.elf \
	serial_virt_tx.elf \
	blk_virt.elf

IMAGES += blk_driver.elf
BLK_MK := blk_driver.mk
# We use a common minimal Linux kernel (no device drivers, no networking) and initrd for both QEMU and Maaxboard
LINUX := 9b59d0094253799d716bfb8bed0ddbf336441364-linux
INITRD := b6a276df6a0e39f76bc8950e975daa2888ad83df-rootfs.cpio.gz

ifeq ($(strip $(MICROKIT_BOARD)), maaxboard)
	NET_DRIV_DIR := imx
	BLK_DRIV_DIR := mmc/imx
	UART_DRIV_DIR := imx
	TIMER_DRIV_DIR := imx
	CPU := cortex-a53
else ifeq ($(strip $(MICROKIT_BOARD)), qemu_virt_aarch64)
	NET_DRIV_DIR := virtio
	BLK_DRIV_DIR := virtio
	UART_DRIV_DIR := arm
	TIMER_DRIV_DIR := arm
	CPU := cortex-a53
	QEMU := qemu-system-aarch64
else
	$(error Unsupported MICROKIT_BOARD given)
endif

TOOLCHAIN := clang
CC := clang
CC_USERLEVEL := zig cc
LD := ld.lld
RANLIB := llvm-ranlib
AR := llvm-ar
AS := llvm-as
TARGET := aarch64-none-elf
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
DTC := dtc
PYTHON ?= python3
OBJCOPY := llvm-objcopy

LIONSOS_DOWNLOADS := https://lionsos.org/downloads/examples/vmfs

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
PLATFORM := meson
SDDF := $(LIONSOS)/dep/sddf
LIBVMM_DIR := $(LIONSOS)/dep/libvmm
LIBVMM_TOOLS := $(LIBVMM_DIR)/tools

METAPROGRAM := $(LIONSOS)/examples/vmfs/meta.py
DTS := $(SDDF)/dts/$(MICROKIT_BOARD).dts
DTB := $(MICROKIT_BOARD).dtb

LWIP := $(SDDF)/network/ipstacks/lwip/src

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
	-I$(SDDF)/include \
	-I${CONFIG_INCLUDE} \
	-I$(LIBVMM_DIR)/include \
	-I$(LIBVMM_DIR)/tools/linux/include \
	-I$(LIONSOS)/components/fs/vmfs \
	-I$(LIONSOS)/include

CFLAGS_USERLEVEL := \
	-g3 \
	-O3 \
	-Wno-unused-command-line-argument \
	-Wall -Werror -Wno-unused-function \
	-D_GNU_SOURCE \
	-target aarch64-linux-gnu \
	-I$(BOARD_DIR)/include \
	-I$(SDDF)/include \
	-I$(LIONSOS)/include \
	-I${CONFIG_INCLUDE}

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a

SYSTEM_FILE := vmfs.system
IMAGE_FILE := vmfs.img
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
include ${LIBVMM_DIR}/vmm.mk
include ${LIONSOS}/components/fs/vmfs/vmfs.mk
include ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk
include ${SDDF}/drivers/network/${NET_DRIV_DIR}/eth_driver.mk
include ${SDDF}/drivers/serial/${UART_DRIV_DIR}/uart_driver.mk
include ${SDDF}/network/components/network_components.mk
include ${SDDF}/serial/components/serial_components.mk
include ${SDDF}/libco/libco.mk
include ${BLK_DRIVER}/${BLK_MK}
include ${BLK_COMPONENTS}/blk_components.mk

micropython.elf: mpy-cross libsddf_util_debug.a libco.a
	cp $(EXAMPLE_DIR)/fs_test.py .
	cp $(EXAMPLE_DIR)/manifest.py .
	cp $(EXAMPLE_DIR)/bench.py .
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

# Compile the FS Driver VM
# Grab the kernel and initrd from LionsOS server
${LINUX}:
	curl -L ${LIONSOS_DOWNLOADS}/$(LINUX) -o $@
${INITRD}:
	curl -L ${LIONSOS_DOWNLOADS}/$(INITRD) -o $@

# If you want to use your own kernel and initrd, then comment out the above
# and specify an *absolute* path like so:
# LINUX=/path/to/linux
# INITRD=/path/to/initrd

vpath %.c $(LIBVMM_DIR)
SYSTEM_DIR := $(EXAMPLE_DIR)/board/$(MICROKIT_BOARD)
FS_VM_USERLEVEL := uio_fs_driver
FS_VM_USERLEVEL_INIT := ${LIONSOS}/components/fs/vmfs/fs_driver_init

fs_vm_initrd: $(INITRD) $(FS_VM_USERLEVEL) $(FS_VM_USERLEVEL_INIT)
	$(LIBVMM_TOOLS)/packrootfs $(INITRD) \
		initrd_staging -o $@ \
		--startup $(FS_VM_USERLEVEL_INIT) \
		--home $(FS_VM_USERLEVEL)

fs_vm.dtb: $(SYSTEM_DIR)/linux_overlayed.dts
	$(DTC) -q -I dts -O dtb $< > $@

fs_driver_vm_image.o: $(LIBVMM_TOOLS)/package_guest_images.S fs_vm.dtb ${LINUX} fs_vm_initrd $(CHECK_FLAGS_BOARD_MD5)
	$(CC) -c -g3 -x assembler-with-cpp \
					-DGUEST_KERNEL_IMAGE_PATH=\"${LINUX}\" \
					-DGUEST_DTB_IMAGE_PATH=\"fs_vm.dtb\" \
					-DGUEST_INITRD_IMAGE_PATH=\"fs_vm_initrd\" \
					-target $(TARGET) \
					$(LIBVMM_TOOLS)/package_guest_images.S -o $@

fs_driver_vmm.o: $(EXAMPLE_DIR)/src/fs_vmm.c
	${CC} ${CFLAGS} -c -o $@ $<

fs_driver_vmm.elf: fs_driver_vmm.o fs_driver_vm_image.o libvmm.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

#

${IMAGES}: libsddf_util_debug.a

$(DTB): $(DTS)
	$(DTC) -q -I dts -O dtb $< > $@

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB) $(LINUX_DTB)
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE) --guest-dtb fs_vm.dtb
	$(OBJCOPY) --update-section .device_resources=uart_driver_device_resources.data uart_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data uart_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .serial_virt_rx_config=serial_virt_rx.data serial_virt_rx.elf

	$(OBJCOPY) --update-section .device_resources=ethernet_driver_device_resources.data eth_driver.elf
	$(OBJCOPY) --update-section .net_driver_config=net_driver.data eth_driver.elf
	$(OBJCOPY) --update-section .net_virt_rx_config=net_virt_rx.data network_virt_rx.elf
	$(OBJCOPY) --update-section .net_virt_tx_config=net_virt_tx.data network_virt_tx.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_micropython_net_copier.data network_copy.elf

	$(OBJCOPY) --update-section .device_resources=blk_driver_device_resources.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_driver_config=blk_driver.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_virt_config=blk_virt.data blk_virt.elf

	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf

	$(OBJCOPY) --update-section .vmm_config=vmm_fs_driver_vmm.data fs_driver_vmm.elf

	$(OBJCOPY) --update-section .timer_client_config=timer_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .net_client_config=net_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_micropython.data micropython.elf

	$(OBJCOPY) --update-section .serial_client_config=serial_client_fs_driver_vmm.data fs_driver_vmm.elf
	$(OBJCOPY) --update-section .blk_client_config=blk_client_fs_driver_vmm.data fs_driver_vmm.elf
	$(OBJCOPY) --update-section .fs_server_config=fs_server_fs_driver_vmm.data fs_driver_vmm.elf

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) \
		--search-path $(BUILD_DIR) \
		--board $(MICROKIT_BOARD) \
		--config $(MICROKIT_CONFIG) \
		-o $(IMAGE_FILE) \
		-r $(REPORT_FILE)

FORCE:

%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

mpy-cross: FORCE
	${MAKE} -C ${LIONSOS}/dep/micropython/mpy-cross BUILD=$(abspath ./mpy_cross)

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
		-device virtio-blk-device,drive=hd \
		-device virtio-net-device,netdev=netdev0 \
		-netdev user,id=netdev0
