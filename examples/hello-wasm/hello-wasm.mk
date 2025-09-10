IMAGES := \
	serial_driver.elf \
	serial_virt_rx.elf \
	serial_virt_tx.elf \
	timer_driver.elf \
	eth_driver.elf \
	network_copy.elf network_virt_rx.elf network_virt_tx.elf \
	wamr.elf

ifeq ($(strip $(MICROKIT_BOARD)), qemu_virt_aarch64)
	SERIAL_DRIV_DIR := arm
	TIMER_DRIV_DIR := arm
	ETHERNET_DRIVER_DIR := virtio
	CPU := cortex-a53
	QEMU := qemu-system-aarch64
else
$(error Unsupported MICROKIT_BOARD given)
endif

TOOLCHAIN := clang
CC := clang
LD := ld.lld
RANLIB := llvm-ranlib
AR := llvm-ar
OBJCOPY := llvm-objcopy
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
DTC := dtc
PYTHON ?= python3

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
ARCH := $(shell grep 'CONFIG_SEL4_ARCH  ' $(BOARD_DIR)/include/kernel/gen_config.h | cut -d' ' -f4)
SDDF := $(LIONSOS)/dep/sddf

ifeq ($(ARCH),aarch64)
	CFLAGS_ARCH := -mcpu=$(CPU)
	TARGET := aarch64-none-elf
else ifeq ($(ARCH),riscv64)
	CFLAGS_ARCH := -march=rv64imafdc
	TARGET := riscv64-none-elf
else
$(error Unsupported ARCH given)
endif

ifeq ($(strip $(TOOLCHAIN)), clang)
	CFLAGS_ARCH += -target $(TARGET)
endif

METAPROGRAM := $(HELLO_WASM_DIR)/meta.py
DTS := $(SDDF)/dts/$(MICROKIT_BOARD).dts
DTB := $(MICROKIT_BOARD).dtb

MUSL_SRC := $(LIONSOS)/dep/musllibc
MUSL := musllibc

CFLAGS := \
	-mtune=$(CPU) \
	-mstrict-align \
	-ffreestanding \
	-O2 \
	-g3 \
	-Wall \
	-Wno-unused-function \
	-Wno-bitwise-op-parentheses \
	-Wno-shift-op-parentheses \
	-I$(BOARD_DIR)/include \
	$(CFLAGS_ARCH) \
	-DBOARD_$(MICROKIT_BOARD) \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(MUSL)/include

LDFLAGS := -L$(BOARD_DIR)/lib -L$(MUSL)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a -lc

SYSTEM_FILE := hello-wasm.system
IMAGE_FILE := hello-wasm.img
REPORT_FILE := report.txt

all: cache.o
CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@

include ${SDDF}/util/util.mk
include ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk
include ${SDDF}/drivers/serial/${SERIAL_DRIV_DIR}/serial_driver.mk
include ${SDDF}/drivers/network/${ETHERNET_DRIVER_DIR}/eth_driver.mk
include ${SDDF}/network/components/network_components.mk
include ${SDDF}/serial/components/serial_components.mk

include $(LIONSOS)/components/wamr/wamr.mk

$(MUSL):
	mkdir -p $@

$(MUSL)/lib/libc.a $(MUSL)/include: ${MUSL_SRC}/Makefile ${MUSL}
	cd ${MUSL} && CC=$(CC) CFLAGS="-target $(TARGET) -mtune=$(CPU)" ${MUSL_SRC}/configure CROSS_COMPILE=llvm- --srcdir=${MUSL_SRC} --prefix=${abspath ${MUSL}} --target=$(TARGET) --with-malloc=oldmalloc --enable-warnings --disable-shared --enable-static
	${MAKE} -C ${MUSL} install

${IMAGES}: libsddf_util_debug.a

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

FORCE:

%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

$(DTB): $(DTS)
	$(DTC) -q -I dts -O dtb $(DTS) > $(DTB)

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE)
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .serial_virt_rx_config=serial_virt_rx.data serial_virt_rx.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .device_resources=ethernet_driver_device_resources.data eth_driver.elf
	$(OBJCOPY) --update-section .net_driver_config=net_driver.data eth_driver.elf
	$(OBJCOPY) --update-section .net_virt_rx_config=net_virt_rx.data network_virt_rx.elf
	$(OBJCOPY) --update-section .net_virt_tx_config=net_virt_tx.data network_virt_tx.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_wamr_net_copier.data network_copy.elf network_copy_wamr.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_wamr.data wamr.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_wamr.data wamr.elf
	$(OBJCOPY) --update-section .lib_sddf_lwip_config=lib_sddf_lwip_config_wamr.data wamr.elf

$(IMAGE_FILE) $(REPORT_FILE): $(MUSL)/include $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

qemu: ${IMAGE_FILE}
	$(QEMU) -machine virt,virtualization=on \
		-cpu cortex-a53 \
		-serial mon:stdio \
		-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
		-m size=2G \
		-nographic \
		-device virtio-net-device,netdev=netdev0 \
		-netdev user,id=netdev0 \
		-global virtio-mmio.force-legacy=false \
		-d guest_errors
