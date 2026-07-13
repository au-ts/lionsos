#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

TOOLCHAIN ?= clang
SDDF := $(LIONSOS)/dep/sddf
LIBVMM_DIR := $(LIONSOS)/dep/libvmm

SUPPORTED_BOARDS := \
  odroidc4 \
  qemu_virt_aarch64

SYSTEM_FILE := vulkan.system
IMAGE_FILE := vulkan.img
REPORT_FILE := report.txt

all: ${IMAGE_FILE}

include ${SDDF}/tools/make/board/common.mk

ifeq ($(strip $(MICROKIT_BOARD)), odroidc4)
  INITRD := 08c10529dc2806559d5c4b7175686a8206e10494-rootfs.cpio.gz
  LINUX := 90c4247bcd24cbca1a3db4b7489a835ce87a486e-linux
else ifeq ($(strip $(MICROKIT_BOARD)), qemu_virt_aarch64)
  INITRD := 8d4a14e2c92a638d68f04832580a57b94e8a4f6b-rootfs.cpio.gz
  LINUX := 75757e2972d5dc528d98cab377e2c74a9e02d6f9-linux
endif

VMM_IMAGE_DIR := ${VULKAN_DIR}/board/$(MICROKIT_BOARD)/framebuffer_vmm_images
VMM_SRC_DIR := ${VULKAN_DIR}/src/vmm
LINUX_DTS := $(VMM_IMAGE_DIR)/linux.dts
LINUX_DTB := linux.dtb

METAPROGRAM := $(VULKAN_DIR)/meta.py

LIONSOS_DOWNLOADS := https://lionsos.org/downloads/examples/kitty

# Essential SDDF drivers for VM operation + VMM
IMAGES := timer_driver.elf \
    vmm.elf \
    serial_driver.elf \
    serial_virt_rx.elf \
    serial_virt_tx.elf \
		display_manager.elf

CFLAGS += \
  -Wno-bitwise-op-parentheses \
  -Wno-shift-op-parentheses \
  -I$(LIONSOS)/include \
  -I$(SDDF)/include \
  -I$(SDDF)/include/microkit 

include $(LIONSOS)/lib/libc/libc.mk

LDFLAGS := -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a -lc

SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include

# Only include the makefiles for core components
SDDF_MAKEFILES := ${SDDF}/util/util.mk \
  ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk \
  ${SDDF}/drivers/serial/${UART_DRIV_DIR}/serial_driver.mk \
  ${SDDF}/serial/components/serial_components.mk

include ${SDDF_MAKEFILES}

LIBVMM_LIBC_INCLUDE := $(LIONS_LIBC)/include

include ${LIBVMM_DIR}/vmm.mk

# Build the VMM for graphics
VMM_OBJS := vmm.o package_guest_images.o
VPATH := ${LIBVMM_DIR}:${VMM_IMAGE_DIR}:${VMM_SRC_DIR}

$(LINUX_DTB): $(LINUX_DTS)
	$(DTC) -q -I dts -O dtb $< > $@

package_guest_images.o: $(LIBVMM_DIR)/tools/package_guest_images.S \
      $(VMM_IMAGE_DIR) $(LINUX) $(INITRD) $(LINUX_DTB)
	$(CC) -c -g3 -x assembler-with-cpp \
        -DGUEST_KERNEL_IMAGE_PATH=\"$(LINUX)\" \
        -DGUEST_DTB_IMAGE_PATH=\"$(LINUX_DTB)\" \
        -DGUEST_INITRD_IMAGE_PATH=\"$(INITRD)\" \
        -target $(TARGET) \
        $< -o $@

vmm.elf: ${VMM_OBJS} libvmm.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

${IMAGES}: $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a

%.o: %.c ${SDDF}/include
	${CC} ${CFLAGS} -c -o $@ $<


DISPLAY_MANAGER_CFLAGS := -I$(LIONSOS)/dep/libvmm/dep/sddf/include \
  -I$(LIONSOS)/dep/libvmm/dep/sddf/examples/gpu/include \
  -I$(LIONSOS)/dep/sddf/drivers/gpu/virtio 


# Compile the Display Manager C code into an object file
display_manager.o: ../src/display_manager/display_manager.c
	$(CC) -c $(CFLAGS) $(DISPLAY_MANAGER_CFLAGS) $< -o $@

# Link the object file into a Microkit ELF executable
display_manager.elf: display_manager.o
	$(LD) $(LDFLAGS) $< $(LIBS) -o $@

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	PYTHONPATH=${SDDF}/tools/meta:$$PYTHONPATH $(PYTHON) $(METAPROGRAM) \
    --sddf $(SDDF) --board $(MICROKIT_BOARD) \
    --dtb $(DTB) --output . --sdf $(SYSTEM_FILE) \
    --guest-dtb $(LINUX_DTB)
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .serial_virt_rx_config=serial_virt_rx.data serial_virt_rx.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .vmm_config=vmm_framebuffer_vmm.data vmm.elf
	touch $@

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

FORCE:

# Fetch VM images
${LINUX}:
	curl -L ${LIONSOS_DOWNLOADS}/$(VULKAN_GRAPHICS_VM_LINUX) -o $@

${INITRD}:
	curl -L ${LIONSOS_DOWNLOADS}/$(VULKAN_GRAPHICS_VM_ROOTFS) -o $@

# Download submodules if missing
${LIBVMM_DIR}/vmm.mk:
	cd ${LIONSOS}; git submodule update --init dep/libvmm

${SDDF}/tools/make/board/common.mk ${SDDF_MAKEFILES} ${LIONSOS}/dep/sddf/include &:
	cd ${LIONSOS}; git submodule update --init dep/sddf

# Note: Removed the unused virtio-net-device flag from QEMU args
qemu: $(IMAGE_FILE)
	$(QEMU) -machine virt,virtualization=on \
      -cpu cortex-a53 \
      -serial mon:stdio \
      -device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
      -m size=2G \
      -global virtio-mmio.force-legacy=false \
      -device virtio-gpu-device,bus=virtio-mmio-bus.8

clean::
	${RM} -f *.elf .depend*
	find . -name \*.[do] |xargs --no-run-if-empty rm

clobber:: clean
	rm -f *.a
	rm -f ${IMAGE_FILE} ${REPORT_FILE}