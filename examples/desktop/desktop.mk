#
# Copyright 2026, LionsOS Contributors
#
# SPDX-License-Identifier: BSD-2-Clause
#

TOOLCHAIN ?= clang
SUPPORTED_BOARDS := qemu_virt_aarch64

SDDF := $(LIONSOS)/dep/sddf
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco
WAMR_DIR := $(LIONSOS)/components/wamr
WAMR_ROOT := $(LIONSOS)/dep/wasm-micro-runtime
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit

GUI_APPS := notes sketch clock
MU_APPS := calculator widgets
IMAGES := gpu_driver.elf gpu_virt.elf timer_driver.elf input_driver.elf input_virt.elf compositor.elf \
	serial_driver.elf serial_virt_tx.elf blk_driver.elf blk_virt.elf fat.elf wasm_host.elf \
	$(addsuffix .elf,$(GUI_APPS) $(MU_APPS))
METAPROGRAM := $(DESKTOP_DIR)/meta.py
SYSTEM_FILE := desktop.system
IMAGE_FILE := desktop.img
REPORT_FILE := report.txt

# WebAssembly apps put on the disk that the wasm_host app slot loads from,
# each with the list of capabilities it is granted, plus files they use
WASM_APPS := hello life mandel reader probe

# With SANDBOX=1, WebAssembly apps run in a sandbox PD that seL4 enforces
# (see wasm_host/sandbox.h). This needs a Microkit SDK whose tool has the
# patch in examples/dynamic_caps, for <cspace> elements stock Microkit lacks.
# The disk then also has an app that never returns, which only a sandbox
# can stop.
ifeq ($(SANDBOX),1)
IMAGES += sandbox.elf
META_FLAGS := --sandbox
WASM_APPS += spin
endif
WASM_FILES := $(addsuffix .wasm,$(WASM_APPS))
WASM_CAPS := $(addprefix $(DESKTOP_DIR)/wasm_apps/,$(addsuffix .caps,$(WASM_APPS)))
WASM_DATA := $(DESKTOP_DIR)/wasm_apps/readme.txt
# Outside /apps, so no app may be granted it (see wasm_apps/probe.c)
DISK_SECRET := $(DESKTOP_DIR)/wasm_apps/secret.txt
DISK_IMAGE := apps_disk.img

# Display mode requested from QEMU. It must fit in GPU_DATA_REGION_SIZE_CLI0.
DESKTOP_XRES ?= 1024
DESKTOP_YRES ?= 768

all: $(IMAGE_FILE) $(WASM_FILES)

include $(SDDF)/tools/make/board/common.mk

# The compat directory comes first so that it can stand in for sDDF virtIO
# headers the GPU driver still expects, see include/compat/sddf/virtio/virtio.h.
# musl's headers trip the two parentheses warnings, as in other examples.
CFLAGS += \
	-Werror \
	-Wno-unused-function \
	-Wno-unused-command-line-argument \
	-Wno-bitwise-op-parentheses \
	-Wno-shift-op-parentheses \
	-I$(DESKTOP_DIR)/include/compat \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(LIONSOS)/include \
	-I$(DESKTOP_DIR)/include

# Every PD uses the LionsOS C library (musl). Only the WebAssembly host calls
# libc_init() to get stdio, files and a heap; the others just use its
# freestanding parts.
include $(LIONSOS)/lib/libc/libc.mk

LDFLAGS := -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib
LIBS := --start-group -lmicrokit -Tmicrokit.ld libsddf_util_debug.a -lc --end-group

SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include
FAT_LIBC_LIB := $(LIONS_LIBC)/lib/libc.a
FAT_LIBC_INCLUDE := $(LIONS_LIBC)/include

SDDF_MAKEFILES := \
	$(SDDF)/util/util.mk \
	$(SDDF)/drivers/gpu/$(GPU_DRIV_DIR)/gpu_driver.mk \
	$(SDDF)/gpu/components/gpu_components.mk \
	$(SDDF)/drivers/timer/$(TIMER_DRIV_DIR)/timer_driver.mk \
	$(SDDF)/drivers/serial/$(UART_DRIV_DIR)/serial_driver.mk \
	$(SDDF)/serial/components/serial_components.mk \
	$(SDDF)/drivers/blk/$(BLK_DRIV_DIR)/blk_driver.mk \
	$(SDDF)/blk/components/blk_components.mk

include $(SDDF_MAKEFILES)
include $(LIONSOS)/components/input/input.mk
include $(LIONSOS)/components/fs/fat/fat.mk

WASM_HOST_CFLAGS := \
	-I$(DESKTOP_DIR)/wasm_host \
	-I$(LIBMICROKITCO_PATH) \
	-I$(WAMR_DIR)/platform \
	-I$(WAMR_ROOT)/core/iwasm/include \
	-I$(WAMR_ROOT)/core/shared/platform/include
LIBMICROKITCO_CFLAGS_wasm_host := $(WASM_HOST_CFLAGS)
include $(LIBMICROKITCO_PATH)/libmicrokitco.mk

$(IMAGES): $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a

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

# Apps using the microui toolkit, which is vendored unmodified
MU_LIB_OBJS := apps/mu_app.o apps/microui.o $(APP_LIB_OBJS)

apps/microui.o: $(DESKTOP_DIR)/apps/microui/microui.c | $(SDDF_LIBC_INCLUDE)
	mkdir -p apps
	$(CC) -c $(CFLAGS) $< -o $@

$(addsuffix .elf,$(MU_APPS)): %.elf: apps/%.o $(MU_LIB_OBJS)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

# The WebAssembly host: WAMR's interpreter with its built-in libc for apps
# (no WASI), on the LionsOS platform layer. Allocations are tagged with their
# usage so that the host can serve linear memory from its pool. Traps print
# the app's call stack, using the function names kept in the modules.
wamr/libvmlib.a: $(WAMR_ROOT)/build-scripts/runtime_lib.cmake | $(LIONS_LIBC)/include
	WAMR_LIBC=$(abspath $(LIONS_LIBC)) CC=$(CC) CPU=$(CPU) TARGET=$(TARGET) \
		cmake -S $(WAMR_DIR)/platform -B wamr \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE=$(WAMR_DIR)/platform/toolchain.cmake \
		-DSHARED_PLATFORM_CONFIG=$(WAMR_DIR)/platform/shared_platform.cmake \
		-DWAMR_BUILD_INTERP=1 \
		-DWAMR_BUILD_LIBC_BUILTIN=1 \
		-DWAMR_BUILD_LIBC_WASI=0 \
		-DWAMR_BUILD_ALLOC_WITH_USAGE=1 -DWAMR_BUILD_DUMP_CALL_STACK=1 -DWAMR_BUILD_CUSTOM_NAME_SECTION=1 > /dev/null
	cmake --build wamr > /dev/null

ifeq ($(SANDBOX),1)
WASM_HOST_DEFS := -DWASM_SANDBOX
WASM_HOST_OBJS := wasm_host/wasm_host.o wasm_host/caps.o wasm_host/sandbox_host.o wasm_host/runner_blob.o
else
WASM_HOST_OBJS := wasm_host/wasm_host.o wasm_host/caps.o wamr/libvmlib.a
endif

wasm_host/wasm_host.o: $(DESKTOP_DIR)/wasm_host/wasm_host.c $(WAMR_ROOT)/build-scripts/runtime_lib.cmake \
		| $(SDDF_LIBC_INCLUDE)
	mkdir -p wasm_host
	$(CC) -c $(CFLAGS) $(WASM_HOST_CFLAGS) $(WASM_HOST_DEFS) $< -o $@

wasm_host/%.o: $(DESKTOP_DIR)/wasm_host/%.c | $(SDDF_LIBC_INCLUDE)
	mkdir -p wasm_host
	$(CC) -c $(CFLAGS) $< -o $@

wasm_host.elf: $(WASM_HOST_OBJS) $(APP_LIB_OBJS) libmicrokitco_wasm_host.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

# The sandbox's runner: WAMR and musl without libmicrokit, linked where the
# host loads it. The host carries it in its image.
wasm_host/runner.o: $(DESKTOP_DIR)/wasm_host/runner.c $(WAMR_ROOT)/build-scripts/runtime_lib.cmake \
		| $(SDDF_LIBC_INCLUDE)
	mkdir -p wasm_host
	$(CC) -c $(CFLAGS) $(WASM_HOST_CFLAGS) $< -o $@

runner.elf: wasm_host/runner.o desktop/gfx.o wamr/libvmlib.a $(LIONS_LIBC)/lib/libc.a $(DESKTOP_DIR)/wasm_host/runner.ld
	$(LD) -T $(DESKTOP_DIR)/wasm_host/runner.ld -L$(LIONS_LIBC)/lib wasm_host/runner.o desktop/gfx.o \
		wamr/libvmlib.a --start-group -lc --end-group -o $@

wasm_host/runner_blob.o: $(DESKTOP_DIR)/wasm_host/runner_blob.S runner.elf
	mkdir -p wasm_host
	$(CC) -c $(CFLAGS) -DRUNNER_ELF=\"runner.elf\" $< -o $@

sandbox.elf: wasm_host/sandbox_stub.o
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

-include $(wildcard desktop/*.d apps/*.d wasm_host/*.d)

# WebAssembly apps are freestanding wasm32 modules. Any clang with the
# WebAssembly target and wasm-ld works; the WASI SDK's is used if available.
ifneq ($(strip $(WASI_SDK)),)
WASM_CC := $(WASI_SDK)/bin/clang
else
WASM_CC ?= clang
endif
WASM_CFLAGS := --target=wasm32 -O2 -nostdlib -ffreestanding -Wall -Werror \
	-Wl,--no-entry -Wl,--allow-undefined -Wl,-z,stack-size=16384 -Wl,--strip-debug

%.wasm: $(DESKTOP_DIR)/wasm_apps/%.c $(DESKTOP_DIR)/wasm_apps/lions.h
	$(WASM_CC) $(WASM_CFLAGS) -o $@ $<

# A disk with one FAT partition holding the apps in /apps. Needs gdisk,
# mkfs.fat (dosfstools) and mtools. The partition starts 1 MiB in.
$(DISK_IMAGE): $(WASM_FILES) $(WASM_CAPS) $(WASM_DATA) $(DISK_SECRET)
	rm -f $@
	$(SDDF)/tools/mkvirtdisk $@ 1 512 16777216 GPT > /dev/null
	mmd -i $@@@1M ::/apps
	mcopy -i $@@@1M $(WASM_FILES) $(WASM_CAPS) $(WASM_DATA) ::/apps/
	mcopy -i $@@@1M $(DISK_SECRET) ::/

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	PYTHONPATH=$(SDDF)/tools/meta:$$PYTHONPATH $(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE) $(META_FLAGS)
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .device_resources=blk_driver_device_resources.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_driver_config=blk_driver.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_virt_config=blk_virt.data blk_virt.elf
	$(OBJCOPY) --update-section .blk_client_config=blk_client_fatfs.data fat.elf
	$(OBJCOPY) --update-section .fs_server_config=fs_server_fatfs.data fat.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_clock.data clock.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_wasm_host.data wasm_host.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_wasm_host.data wasm_host.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_wasm_host.data wasm_host.elf
	$(OBJCOPY) --update-section .input_driver_config=input_driver_keyboard.data input_driver.elf input_keyboard.elf
	$(OBJCOPY) --update-section .input_driver_config=input_driver_tablet.data input_driver.elf input_tablet.elf

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

# Each virtIO device is pinned to the MMIO transport meta.py (or, for the
# disk, sDDF's board description) expects
QEMU_GPU := virtio-gpu-device,bus=virtio-mmio-bus.31,xres=$(DESKTOP_XRES),yres=$(DESKTOP_YRES),edid=off,blob=off,max_outputs=1,indirect_desc=off,event_idx=off
QEMU_KEYBOARD := virtio-keyboard-device,bus=virtio-mmio-bus.30
QEMU_TABLET := virtio-tablet-device,bus=virtio-mmio-bus.29
QEMU_DISK := virtio-blk-device,drive=hd,bus=virtio-mmio-bus.1

QEMU_CMD := $(QEMU) -machine virt,virtualization=on \
	-cpu cortex-a53 \
	-serial mon:stdio \
	-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
	-m size=2G \
	-device $(QEMU_GPU) \
	-device $(QEMU_KEYBOARD) \
	-device $(QEMU_TABLET) \
	-drive file=$(DISK_IMAGE),if=none,format=raw,id=hd \
	-device $(QEMU_DISK) \
	-global virtio-mmio.force-legacy=false

# The included makefiles and sources are implicit dependencies of the build,
# so check out the submodules providing them if they are missing.
$(SDDF)/tools/make/board/common.mk $(SDDF_MAKEFILES) $(SDDF)/include &:
	cd $(LIONSOS); git submodule update --init dep/sddf

$(LIBMICROKITCO_PATH)/libmicrokitco.mk:
	cd $(LIONSOS); git submodule update --init dep/libmicrokitco

$(WAMR_ROOT)/build-scripts/runtime_lib.cmake:
	cd $(LIONSOS); git submodule update --init dep/wasm-micro-runtime

qemu: $(IMAGE_FILE) $(DISK_IMAGE)
	$(QEMU_CMD)

clean::
	rm -rf desktop apps wasm_host wamr

clobber:: clean
	rm -f compositor.elf $(addsuffix .elf,$(GUI_APPS) $(MU_APPS)) wasm_host.elf input_keyboard.elf input_tablet.elf \
		runner.elf sandbox.elf \
		$(WASM_FILES) $(DISK_IMAGE) $(IMAGE_FILE) $(REPORT_FILE) $(SYSTEM_FILE) *.data $(DTB)
