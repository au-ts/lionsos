# Makefile for webserver.
#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This makefile will be copied into the Build directory and used from there.
#
BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
SDDF := $(LIONSOS)/dep/sddf

ifeq (${MICROKIT_BOARD},odroidc4)
	TIMER_DRIVER_DIR := meson
	ETHERNET_DRIVER_DIR := meson
	SERIAL_DRIVER_DIR := meson

	CPU := cortex-a55
	TARGET := aarch64-none-elf
else ifeq (${MICROKIT_BOARD},maaxboard)
	TIMER_DRIVER_DIR := imx
	ETHERNET_DRIVER_DIR := imx
	SERIAL_DRIVER_DIR := imx

	CPU := cortex-a53
	TARGET := aarch64-none-elf
else ifeq (${MICROKIT_BOARD},qemu_virt_aarch64)
	TIMER_DRIVER_DIR := arm
	ETHERNET_DRIVER_DIR := virtio/mmio
	SERIAL_DRIVER_DIR := arm

	CPU := cortex-a53
	TARGET := aarch64-none-elf
else ifeq (${MICROKIT_BOARD},x86_64_nehalem)
	TIMER_DRIVER_DIR := hpet
	ETHERNET_DRIVER_DIR := virtio/pci
	SERIAL_DRIVER_DIR := pc99

	CPU := nehalem
	TARGET := x86_64-pc-elf
else
$(error Unsupported MICROKIT_BOARD given)
endif

ifeq ($(findstring aarch64,$(TARGET)),aarch64)
	MUSL_TARGET := aarch64
	ARCH_CFLAGS := -mstrict-align -O2
	QEMU := qemu-system-aarch64
	ARCH_QEMU_BOOT_IMAGE := x86_grub.iso
	ARCH_QEMU_FLAGS = -machine virt,virtualization=on \
						-cpu cortex-a53 \
						-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
						-device virtio-net-device,netdev=netdev0 \
						-netdev user,id=netdev0,hostfwd=tcp::5555-10.0.2.16:80 \
						-global virtio-mmio.force-legacy=false

else ifeq ($(findstring x86_64,$(TARGET)),x86_64)
	QEMU := qemu-system-x86_64
	MUSL_TARGET := x86_64
# This is intentionally left empty as there are no device trees on x86.
	DTS :=
# @billn: investigate comp op on x86 with clang, irq not worky
	ARCH_CFLAGS := 
	ARCH_QEMU_FLAGS = -machine q35,kernel-irqchip=split \
						-cpu Nehalem,+fsgsbase,+pdpe1gb,+pcid,+invpcid,+xsave,+xsaves,+xsaveopt,+vmx,+vme \
						-device intel-iommu \
						-cdrom x86_grub.iso \
						-netdev user,id=net0,hostfwd=tcp::5555-10.0.2.16:80 \
						-device virtio-net-pci,netdev=net0
	
	ARCH_MICROKIT_FLAG := --x86-machine /opt/billn/x86/microkit_mat_rebased/example/x86_64_nehalem/hello/machine.json
else
  $(error unknown target)
endif

ifeq ($(strip $(LIBGCC)),)
LIBGCC=$(dir $(realpath $(shell $(TARGET)-gcc --print-file-name libgcc.a)))
endif
ifeq ($(strip $(LIBC)),)
LIBC=$(dir $(realpath $(shell $(TARGET)-gcc --print-file-name libc.a)))
endif
ifeq ($(strip $(LIBMATH)),)
LIBMATH=$(dir $(realpath $(shell $(TARGET)-gcc --print-file-name libm.a)))
endif

export CPU

TOOLCHAIN := clang
CC := clang
LD := ld.lld
AR := llvm-ar
RANLIB := llvm-ranlib
OBJCOPY := llvm-objcopy
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
PYTHON ?= python3
DTC := dtc
CROSS_COMPILE := $(TARGET)-

NFS=$(LIONSOS)/components/fs/nfs
MUSL_SRC := $(LIONSOS)/dep/musllibc
MUSL := musllibc
MICRODOT := ${LIONSOS}/dep/microdot/src

METAPROGRAM := $(WEBSERVER_SRC_DIR)/meta.py
DTS ?= $(SDDF)/dts/$(MICROKIT_BOARD).dts
DTB := $(MICROKIT_BOARD).dtb

IMAGES := timer_driver.elf eth_driver.elf micropython.elf nfs.elf \
	  network_copy.elf network_virt_rx.elf network_virt_tx.elf \
	  serial_driver.elf serial_virt_tx.elf

SYSTEM_FILE := webserver.system

# @billn: why does compiler optimisation break irq??
CFLAGS := \
	-mtune=$(CPU) \
	-ffreestanding \
	-g3 \
	-MD \
	-MP \
	-Wall \
	-Wno-unused-function \
	-I$(BOARD_DIR)/include \
	-target $(TARGET) \
	-DBOARD_$(MICROKIT_BOARD) \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	$(ARCH_CFLAGS)

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a

IMAGE_FILE = webserver.img
REPORT_FILE = report.txt

all: $(IMAGE_FILE)
${IMAGES}: libsddf_util_debug.a

CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@

MICROPYTHON_LIBMATH = $(LIBMATH)
MICROPYTHON_LIBC = $(LIBC)
MICROPYTHON_LIBGCC = $(LIBGCC)
MICROPYTHON_CONFIG_INCLUDE = $(CONFIG_INCLUDE)
MICROPYTHON_EXEC_MODULE := webserver.py
MICROPYTHON_FROZEN_MANIFEST = manifest.py
MICROPYTHON_CROSS_COMPILE = $(CROSS_COMPILE)

include $(LIONSOS)/components/micropython/micropython.mk

manifest.py: webserver.py config.py
webserver.py: $(MICRODOT) config.py

config.py: ${CHECK_FLAGS_BOARD_MD5}
	echo "base_dir='$(WEBSITE_DIR)'" > config.py

%.py: ${WEBSERVER_SRC_DIR}/%.py
	cp $< $@

$(MUSL):
	mkdir -p $@

$(MUSL)/lib/libc.a $(MUSL)/include: ${MUSL_SRC}/Makefile ${MUSL}
	cd ${MUSL} && CC=$(TARGET)-gcc CROSS_COMPILE=$(CROSS_COMPILE) ${MUSL_SRC}/configure --srcdir=${MUSL_SRC} --prefix=${abspath ${MUSL}} --target=$(MUSL_TARGET) --with-malloc=oldmalloc --enable-warnings --disable-shared --enable-static
	${MAKE} -C ${MUSL} install -j$(nproc)

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

SDDF_MAKEFILES := ${SDDF}/util/util.mk \
		  ${SDDF}/drivers/timer/${TIMER_DRIVER_DIR}/timer_driver.mk \
		  ${SDDF}/drivers/network/${ETHERNET_DRIVER_DIR}/eth_driver.mk \
		  ${SDDF}/drivers/serial/${SERIAL_DRIVER_DIR}/serial_driver.mk \
		  ${SDDF}/network/components/network_components.mk \
		  ${SDDF}/network/lib_sddf_lwip/lib_sddf_lwip.mk \
		  ${SDDF}/serial/components/serial_components.mk

include ${SDDF_MAKEFILES}
include $(NFS)/nfs.mk

$(DTB): $(DTS)
ifneq ($(strip $(DTS)),)
	$(DTC) -q -I dts -O dtb $(DTS) > $(DTB)
endif

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE) --nfs-server $(NFS_SERVER) --nfs-dir $(NFS_DIRECTORY)
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .device_resources=ethernet_driver_device_resources.data eth_driver.elf
	$(OBJCOPY) --update-section .net_driver_config=net_driver.data eth_driver.elf
	$(OBJCOPY) --update-section .net_virt_rx_config=net_virt_rx.data network_virt_rx.elf
	$(OBJCOPY) --update-section .net_virt_tx_config=net_virt_tx.data network_virt_tx.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_micropython_net_copier.data network_copy.elf network_copy_micropython.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_nfs_net_copier.data network_copy.elf network_copy_nfs.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .net_client_config=net_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .net_client_config=net_client_nfs.data nfs.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_nfs.data nfs.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_nfs.data nfs.elf
	$(OBJCOPY) --update-section .fs_server_config=fs_server_nfs.data nfs.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .nfs_config=nfs_config.data nfs.elf
	$(OBJCOPY) --update-section .lib_sddf_lwip_config=lib_sddf_lwip_config_nfs.data nfs.elf
	$(OBJCOPY) --update-section .lib_sddf_lwip_config=lib_sddf_lwip_config_micropython.data micropython.elf

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(ARCH_MICROKIT_FLAG) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

qemu: ${IMAGE_FILE}
ifeq ($(findstring x86_64,$(TARGET)),x86_64)
	mkdir -p grub_iso_staging/boot/grub
	cp $(IMAGE_FILE) grub_iso_staging/loader.img
	cp $(WEBSERVER_SRC_DIR)/x86_grub.cfg grub_iso_staging/boot/grub/grub.cfg
	grub-mkrescue -d /usr/lib/grub/i386-pc -o x86_grub.iso grub_iso_staging
endif

	$(QEMU) -serial mon:stdio \
			-m size=4G \
			-nographic \
			-display none \
			$(ARCH_QEMU_FLAGS)
FORCE: ;

$(LIONSOS)/dep/micropython/py/mkenv.mk ${LIONSOS}/dep/micropython/mpy-cross:
	cd ${LIONSOS}; git submodule update --init dep/micropython
	cd ${LIONSOS}/dep/micropython && git submodule update --init lib/micropython-lib
${LIONSOS}/dep/libmicrokitco/libmicrokitco.mk:
	cd ${LIONSOS}; git submodule update --init dep/libmicrokitco

${MICRODOT}:
	cd ${LIONSOS}; git submodule update --init dep/microdot

${MUSL_SRC}/Makefile:
	cd ${LIONSOS}; git submodule update --init dep/musllibc

${SDDF_MAKEFILES} &:
	cd ${LIONSOS}; git submodule update --init dep/sddf
