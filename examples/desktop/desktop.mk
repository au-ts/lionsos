#
# Copyright 2026, LionsOS Contributors
#
# SPDX-License-Identifier: BSD-2-Clause
#

TOOLCHAIN ?= clang
SUPPORTED_BOARDS := qemu_virt_aarch64

SDDF := $(LIONSOS)/dep/sddf
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit

IMAGES := gpu_driver.elf gpu_virt.elf timer_driver.elf input_driver.elf input_virt.elf desktop.elf
METAPROGRAM := $(DESKTOP_DIR)/meta.py
SYSTEM_FILE := desktop.system
IMAGE_FILE := desktop.img
REPORT_FILE := report.txt

# Display mode requested from QEMU. It must fit in GPU_DATA_REGION_SIZE_CLI0.
DESKTOP_XRES ?= 1024
DESKTOP_YRES ?= 768

all: $(IMAGE_FILE)

include $(SDDF)/tools/make/board/common.mk

# The compat directory comes first so that it can stand in for sDDF virtIO
# headers the GPU driver still expects, see include/compat/sddf/virtio/virtio.h.
CFLAGS += \
	-Werror \
	-Wno-unused-function \
	-Wno-unused-command-line-argument \
	-I$(DESKTOP_DIR)/include/compat \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(LIONSOS)/include \
	-I$(DESKTOP_DIR)/include

SDDF_CUSTOM_LIBC := 1
LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := --start-group -lmicrokit -Tmicrokit.ld libsddf_util_debug.a --end-group

SDDF_MAKEFILES := \
	$(SDDF)/util/util.mk \
	$(SDDF)/drivers/gpu/$(GPU_DRIV_DIR)/gpu_driver.mk \
	$(SDDF)/gpu/components/gpu_components.mk \
	$(SDDF)/drivers/timer/$(TIMER_DRIV_DIR)/timer_driver.mk

include $(SDDF_MAKEFILES)
include $(LIONSOS)/components/input/input.mk

$(IMAGES): libsddf_util_debug.a

DESKTOP_OBJS := desktop/desktop.o desktop/gfx.o desktop/keymap.o

desktop/%.o: $(DESKTOP_DIR)/src/%.c | $(SDDF_LIBC_INCLUDE)
	mkdir -p desktop
	$(CC) -c $(CFLAGS) $< -o $@

desktop.elf: $(DESKTOP_OBJS)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

-include $(DESKTOP_OBJS:.o=.d)

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	PYTHONPATH=$(SDDF)/tools/meta:$$PYTHONPATH $(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE)
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_desktop.data desktop.elf
	$(OBJCOPY) --update-section .input_driver_config=input_driver_keyboard.data input_driver.elf input_keyboard.elf
	$(OBJCOPY) --update-section .input_driver_config=input_driver_tablet.data input_driver.elf input_tablet.elf

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

# Each virtIO device is pinned to the MMIO transport meta.py expects
QEMU_GPU := virtio-gpu-device,bus=virtio-mmio-bus.31,xres=$(DESKTOP_XRES),yres=$(DESKTOP_YRES),edid=off,blob=off,max_outputs=1,indirect_desc=off,event_idx=off
QEMU_KEYBOARD := virtio-keyboard-device,bus=virtio-mmio-bus.30
QEMU_TABLET := virtio-tablet-device,bus=virtio-mmio-bus.29

QEMU_CMD := $(QEMU) -machine virt,virtualization=on \
	-cpu cortex-a53 \
	-serial mon:stdio \
	-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
	-m size=2G \
	-device $(QEMU_GPU) \
	-device $(QEMU_KEYBOARD) \
	-device $(QEMU_TABLET) \
	-global virtio-mmio.force-legacy=false

# The included makefiles are implicit dependencies of the build, so check out
# the sDDF submodule if they are missing.
$(SDDF)/tools/make/board/common.mk $(SDDF_MAKEFILES) $(SDDF)/include &:
	cd $(LIONSOS); git submodule update --init dep/sddf

qemu: $(IMAGE_FILE)
	$(QEMU_CMD)

clean::
	rm -rf desktop

clobber:: clean
	rm -f desktop.elf input_keyboard.elf input_tablet.elf $(IMAGE_FILE) $(REPORT_FILE) $(SYSTEM_FILE) *.data $(DTB)
