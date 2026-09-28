#
# Copyright 2026, LionsOS Contributors
#
# SPDX-License-Identifier: BSD-2-Clause
#

TOOLCHAIN ?= clang
SUPPORTED_BOARDS := qemu_virt_aarch64

SDDF := $(LIONSOS)/dep/sddf
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit

GUI_APPS := notes sketch clock
MU_APPS := calculator widgets
IMAGES := gpu_driver.elf gpu_virt.elf timer_driver.elf input_driver.elf input_virt.elf compositor.elf \
	$(addsuffix .elf,$(GUI_APPS) $(MU_APPS))
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

GFX_OBJS := desktop/gfx.o desktop/keymap.o
APP_LIB_OBJS := apps/gui_app.o $(GFX_OBJS)

desktop/%.o: $(DESKTOP_DIR)/src/%.c | $(SDDF_LIBC_INCLUDE)
	mkdir -p desktop
	$(CC) -c $(CFLAGS) $< -o $@

apps/%.o: $(DESKTOP_DIR)/apps/%.c | $(SDDF_LIBC_INCLUDE)
	mkdir -p apps
	$(CC) -c $(CFLAGS) $< -o $@

compositor.elf: desktop/compositor.o $(GFX_OBJS)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(addsuffix .elf,$(GUI_APPS)): %.elf: apps/%.o $(APP_LIB_OBJS)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

# Apps using the microui toolkit. microui.c is vendored unmodified, so the
# few libc functions it needs beyond sDDF's are declared by a forced include.
MU_LIB_OBJS := apps/mu_app.o apps/mu_port.o apps/microui.o $(APP_LIB_OBJS)

apps/microui.o: $(DESKTOP_DIR)/apps/microui/microui.c | $(SDDF_LIBC_INCLUDE)
	mkdir -p apps
	$(CC) -c $(CFLAGS) -include $(DESKTOP_DIR)/apps/mu_port.h $< -o $@

$(addsuffix .elf,$(MU_APPS)): %.elf: apps/%.o $(MU_LIB_OBJS)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

-include $(wildcard desktop/*.d apps/*.d)

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	PYTHONPATH=$(SDDF)/tools/meta:$$PYTHONPATH $(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE)
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_clock.data clock.elf
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
	rm -rf desktop apps

clobber:: clean
	rm -f compositor.elf $(addsuffix .elf,$(GUI_APPS) $(MU_APPS)) input_keyboard.elf input_tablet.elf $(IMAGE_FILE) $(REPORT_FILE) $(SYSTEM_FILE) *.data $(DTB)
