#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

QEMU := qemu-system-aarch64
DTC := dtc
PYTHON ?= python3

METAPROGRAM := $(TOP)/meta.py

MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
DEBUGGER:=${TOP}/apps/debugger
UTIL:=$(SDDF)/util
SERIAL_COMPONENTS := $(SDDF)/serial/components
SERIAL_DRIVER := $(SDDF)/drivers/serial/$(SERIAL_DRIV_DIR)

BOARD := $(MICROKIT_BOARD)
BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
IMAGE_FILE := loader.img
REPORT_FILE := report.txt
SYSTEM_FILE := sddf_serial_example.system
DTS := $(SDDF)/dts/$(MICROKIT_BOARD).dts
DTB := $(MICROKIT_BOARD).dtb
METAPROGRAM := $(TOP)/meta.py

vpath %.c ${SDDF} ${DEBUGGER}

IMAGES := debugger.elf serial_driver.elf serial_virt_tx.elf serial_virt_rx.elf ping.elf pong.elf

CFLAGS := -mcpu=$(CPU) \
	  -mstrict-align \
	  -ffreestanding \
	  -g3 -O3 -Wall \
	  -Wno-unused-function \
	  -DMICROKIT_CONFIG_$(MICROKIT_CONFIG) \
	  -DMICROKIT \
	  -nostdlib \
	  -I$(BOARD_DIR)/include \
	  -I$(SDDF)/include \
	  -I$(SDDF)/include/microkit \
	  -I$(SDDF)/libco \
	  -I$(LIBGDB_DIR)/include \
	  -I$(LIBGDB_DIR)/arch_include \
	  -MD \
	  -MP

LDFLAGS := -L$(BOARD_DIR)/lib -L${LIBC}
LIBS := --start-group -lmicrokit -Tmicrokit.ld -lc libsddf_util_debug.a libvspace.a --end-group

CHECK_FLAGS_BOARD_MD5 := .board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@

%.elf: %.o
	$(LD) $(LDFLAGS) $< $(LIBS) -o $@

debugger.o: $(TOP)/apps/debugger/debugger.c
	$(CC) -c $(CFLAGS) $(TOP)/apps/debugger/debugger.c -o $@ # There is something weird with $@ on the second build here???


ping.o: $(TOP)/apps/ping/ping.c
	$(CC) -c $(CFLAGS) $(TOP)/apps/ping/ping.c -o $@

pong.o: $(TOP)/apps/pong/pong.c
	$(CC) -c $(CFLAGS) $(TOP)/apps/pong/pong.c -o $@

DEBUGGER_OBJS := debugger.o

DEPS := $(DEBUGGER_OBJS:.o=.d)

all: loader.img

${DEBUGGER_OBJS}: ${CHECK_FLAGS_BOARD_MD5}
debugger.elf: $(DEBUGGER_OBJS) libsddf_util.a libgdb.a libco.a
	$(LD) $(LDFLAGS) $(DEBUGGER_OBJS) libsddf_util.a libvspace.a libgdb.a libco.a $(LIBS) -o $@

# Need to build libsddf_util_debug.a because it's included in LIBS
# for the unimplemented libc dependencies
${IMAGES}: libsddf_util_debug.a libvspace.a

$(DTB): $(DTS)
	dtc -q -I dts -O dtb $(DTS) > $(DTB)

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE)
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .serial_virt_rx_config=serial_virt_rx.data serial_virt_rx.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_debugger.data debugger.elf

${IMAGE_FILE} $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

include ${SDDF}/util/util.mk
include ${SERIAL_DRIVER}/serial_driver.mk
include ${SERIAL_COMPONENTS}/serial_components.mk
include $(LIBGDB_DIR)/libgdb.mk
include ${SDDF}/libco/libco.mk
include $(LIBVSPACE_DIR)/libvspace.mk

qemu: $(IMAGE_FILE)
	$(QEMU) -machine virt,virtualization=on \
              -cpu cortex-a53 \
              -serial mon:stdio \
              -device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
              -m size=2G \
              -nographic \
              -device virtio-serial-device \
              -chardev pty,id=virtcon \
              -device virtconsole,chardev=virtcon \
              -global virtio-mmio.force-legacy=false \
              -d guest_errors

clean::
	${RM} -f *.elf .depend* $
	find . -name \*.[do] |xargs --no-run-if-empty rm

clobber:: clean
	rm -f *.a
	rm -f ${IMAGE_FILE} ${REPORT_FILE}

-include $(DEPS)
