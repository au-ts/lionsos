#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
SUPPORTED_BOARDS := \
	qemu_virt_aarch64 \
	maaxboard

IMAGES := \
	debugger.elf \
	faulter.elf \
	serial_driver.elf \
	serial_virt_tx.elf \
	serial_virt_rx.elf \
	eth_driver.elf \
	network_virt_rx.elf \
	network_virt_tx.elf \
	network_copy.elf timer_driver.elf


TOOLCHAIN ?= clang
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
export BOARD := $(MICROKIT_BOARD)
DTB := $(MICROKIT_BOARD).dtb
DTS := $(SDDF)/dts/$(MICROKIT_BOARD).dts

SDDF := $(LIONSOS)/dep/sddf
SYSTEM_FILE := gdb_component.system
IMAGE_FILE := gdb_component.img
REPORT_FILE := report.txt
DEBUGGER_BACKEND ?= serial
ifeq ($(DEBUGGER_BACKEND),net)
    UART_DRIV_DIR := arm
else
    UART_DRIV_DIR := virtio
endif

include ${SDDF}/tools/make/board/common.mk

SERIAL_COMPONENTS := $(SDDF)/serial/components
SERIAL_DRIVER := $(SDDF)/drivers/serial/$(UART_DRIV_DIR)

LIBGDB_DIR=$(LIONSOS)/dep/libgdb
LIBVSPACE_DIR=$(LIBGDB_DIR)/libvspace

ETHERNET_DRIVER:=$(SDDF)/drivers/network/$(NET_DRIV_DIR)
SERIAL_DRIVER := $(SDDF)/drivers/serial/$(UART_DRIV_DIR)
TIMER_DRIVER:=$(SDDF)/drivers/timer/$(TIMER_DRIV_DIR)
LWIP:=$(SDDF)/network/ipstacks/lwip/src


METAPROGRAM := $(GDB_COMPONENT_DIR)/meta.py
DEBUGGER_DIR := $(LIONSOS)/components/debugger

CFLAGS += \
	-DMICROKIT \
	-Wno-bitwise-op-parentheses \
	-Wno-shift-op-parentheses \
	-Wno-unused-function \
	-Wno-tautological-constant-out-of-range-compare \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/libco \
	-I$(SDDF)/include/microkit \
	-DMAX_FDS=8 \
	-I$(LIBGDB_DIR)/include \
	-I$(LIBGDB_DIR)/arch_include \
	-I$(LIBVSPACE_DIR) \
	-I$(LWIP)/include \
	-I$(LWIP)/include/ipv4 \
	-ggdb

QEMU_ARGS := -machine virt,virtualization=on \
		-cpu cortex-a53 \
		-serial mon:stdio \
		-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
		-m size=2G \
		-nographic \
		-global virtio-mmio.force-legacy=false \
		-d guest_errors

ifeq ($(DEBUGGER_BACKEND),net)
QEMU_ARGS += -device virtio-net-device,netdev=netdev0 \
    -netdev user,id=netdev0,hostfwd=tcp::1234-:1234
else
QEMU_ARGS += -device virtio-serial-device \
        -chardev pty,id=virtcon \
        -device virtconsole,chardev=virtcon
endif

include $(LIONSOS)/lib/libc/libc.mk

LDFLAGS := -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib -L$(GDB_COMPONENT_DIR)/build
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a -lc libvspace.a


SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include
include ${SDDF}/util/util.mk
include ${SDDF}/libco/libco.mk
include ${SERIAL_DRIVER}/serial_driver.mk
include ${SERIAL_COMPONENTS}/serial_components.mk
include $(LIBGDB_DIR)/libgdb.mk
include $(LIBVSPACE_DIR)/libvspace.mk
include $(DEBUGGER_DIR)/debugger.mk
include ${SDDF}/network/components/network_components.mk
include ${SDDF}/network/lib_sddf_lwip/lib_sddf_lwip.mk
include ${TIMER_DRIVER}/timer_driver.mk
include ${ETHERNET_DRIVER}/eth_driver.mk


all: ${IMAGE_FILE}

${IMAGES}: $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a libvspace.a

faulter.o: $(GDB_COMPONENT_DIR)/faulter.c | $(LIONS_LIBC)/include
	${CC} ${CFLAGS} -c -o $@ $<

faulter.elf: faulter.o
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

FORCE:

$(DTB): $(DTS)
	dtc -q -I dts -O dtb $(DTS) > $(DTB)


$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	PYTHONPATH="${DEBUGGER_DIR}:${SDDF}/tools/meta:$$PYTHONPATH:$(PYTHONPATH)" $(PYTHON) $(METAPROGRAM) \
		--sddf $(SDDF) --board $(MICROKIT_BOARD)_$(DEBUGGER_BACKEND) --output . --sdf $(SYSTEM_FILE) --dtb $(DTB)
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_debugger.data debugger.elf
ifeq ($(DEBUGGER_BACKEND),net)
	$(OBJCOPY) --update-section .device_resources=ethernet_driver_device_resources.data eth_driver.elf
	$(OBJCOPY) --update-section .net_driver_config=net_driver.data eth_driver.elf
	$(OBJCOPY) --update-section .net_virt_rx_config=net_virt_rx.data network_virt_rx.elf
	$(OBJCOPY) --update-section .net_virt_tx_config=net_virt_tx.data network_virt_tx.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_debugger_net_copier.data network_copy.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_debugger.data debugger.elf
	$(OBJCOPY) --update-section .net_client_config=net_client_debugger.data debugger.elf
	$(OBJCOPY) --update-section .lib_sddf_lwip_config=lib_sddf_lwip_config_debugger.data debugger.elf
else
	$(OBJCOPY) --update-section .serial_virt_rx_config=serial_virt_rx.data serial_virt_rx.elf
endif


$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

qemu_disk:
	$(SDDF)/tools/mkvirtdisk $@ 1 512 16777216 GPT

qemu: ${IMAGE_FILE} qemu_disk
	$(QEMU) $(QEMU_ARGS)


clean::
	${RM} -rf ${IMAGES} faulter.o faulter.elf
