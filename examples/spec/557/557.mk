#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

IMAGES := pager.elf client.elf \
	serial_driver.elf \
	serial_virt_tx.elf
ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
ifeq ($(strip $(SDDF)),)
$(error SDDF must be specified)
endif
ifeq ($(strip $(LIONSOS)),)
$(error LIONSOS must be specified)
endif
ifeq ($(strip $(TOP)),)
$(error TOP must be specified)
endif
# SERIAL_DRIV_DIR, CPU, PAGER_INIT_FRAMES, QEMU and SUPPORTED_BOARDS, all
# selected by MICROKIT_BOARD.
include $(LIONSOS)/examples/board.mk
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
GEN_CONFIG_H := $(BOARD_DIR)/include/kernel/gen_config.h
ifeq ($(wildcard $(GEN_CONFIG_H)),)
$(error Could not find $(GEN_CONFIG_H) -- check that MICROKIT_SDK ($(MICROKIT_SDK)) is a valid Microkit SDK containing a build for board $(MICROKIT_BOARD)/$(MICROKIT_CONFIG))
endif
ARCH := $(shell grep 'CONFIG_SEL4_ARCH  ' $(GEN_CONFIG_H) | cut -d' ' -f4)
SDDF := $(LIONSOS)/dep/sddf
ifeq ($(ARCH),aarch64)
	CFLAGS_ARCH := -mcpu=$(CPU)
	TARGET := aarch64-none-elf
else
$(error Unsupported ARCH given)
endif

BUILD_DIR ?= build

ifeq ($(strip $(TOOLCHAIN)), clang)
	CFLAGS_ARCH += -target $(TARGET)
endif

IMAGE_FILE := loader.img
REPORT_FILE  := report.txt
SYSTEM_FILE := 557.system

vpath %.c ${SDDF} ${TOP}
CONFIGS_INCLUDE := ${TOP}
SDDF_CUSTOM_LIBC := 1

BENCHMARKS_DIR := $(LIONSOS)/dep/benchmarking
# The SPEC CPU benchmark, driven by the client out of the ramfs image below.
SPEC_BENCHMARKS ?= 557.xz_r
SPEC_DIRS := $(foreach b,$(SPEC_BENCHMARKS),$(BENCHMARKS_DIR)/$(b)/src)
# Each benchmark ships one Make snippet per OS, named after its number:
# 519.lbm_r -> src/519-lions.mk, which builds $(BUILD_DIR)/519.a.
spec_num = $(firstword $(subst ., ,$(1)))
SPEC_LIBS := $(foreach b,$(SPEC_BENCHMARKS),$(call spec_num,$(b)).a)
BENCHMARK_LIBS := $(SPEC_LIBS)
$(foreach b,$(SPEC_BENCHMARKS),\
  $(if $(wildcard $(BENCHMARKS_DIR)/$(b)/src/$(call spec_num,$(b))-lions.mk),,\
    $(error Could not find $(call spec_num,$(b))-lions.mk in $(BENCHMARKS_DIR)/$(b)/src -- run `make submodules`)))
# Only client.c includes the benchmark headers, so they stay off everything
# else's include path.
BENCHMARK_INCLUDES := $(addprefix -I,$(SPEC_DIRS))
BENCHMARK_DEFINES := $(foreach b,$(SPEC_BENCHMARKS),-DBENCH_$(firstword $(subst ., ,$(b))))
# Where pager_config.h and pager_instrumentation.h live.
PAGER_CONFIG_DIR := $(LIONSOS)/components/pager/config

# RAM_FATFS in the ramfs config moves the client's files from file.c (which has
# no file system PD to talk to here) to FatFs over a RAM disk inside the client.
# The disk starts out as a cpio archive of the RAMFS_ROOTS merged together.
RAMFS_CONFIG_DIR := $(LIONSOS)/components/fs/ramfs/config
HASH := \#
RAM_FATFS := $(shell grep -qE '^[[:space:]]*$(HASH)[[:space:]]*define[[:space:]]+RAM_FATFS([[:space:]]|$$)' $(RAMFS_CONFIG_DIR)/fat_config.h && echo 1)
RAMFS_ROOTS ?= $(foreach b,$(SPEC_BENCHMARKS),$(BENCHMARKS_DIR)/$(b)/data/test/input)
RAMFS_SIZE ?= 0x4000000
# "control" is SPEC's runspec metadata that no benchmark reads.
RAMFS_STAGE := ramfs_root
RAMFS_EXCLUDE := control

CFLAGS := \
	-mstrict-align \
	-ffreestanding \
	-O3 \
	-g3 \
	-Wall \
	-Wno-unused-function \
	-Wno-bitwise-op-parentheses \
	-Wno-shift-op-parentheses \
	-I$(BOARD_DIR)/include \
	$(CFLAGS_ARCH) \
	-DBOARD_$(MICROKIT_BOARD) \
	-DINIT_FRAMES=$(PAGER_INIT_FRAMES) \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(TOP) \
	-I$(PAGER_CONFIG_DIR)
# The client is demand-paged by the pager PD, so libc's brk/mmap/munmap and
# fork/clone become PPCs to it rather than a static morecore arena.
LIBC_PAGER := 1
include $(LIONSOS)/lib/libc/libc.mk
include $(SDDF)/tools/make/board/common.mk
LDFLAGS := -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a
CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@

METAPROGRAM := $(TOP)/meta.py

all: $(IMAGE_FILE)

SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include
include ${SDDF}/util/util.mk
include ${SDDF}/drivers/serial/${SERIAL_DRIV_DIR}/serial_driver.mk
include ${SDDF}/serial/components/serial_components.mk
include $(LIONSOS)/components/pager/pager.mk
# musl has to be configured before the pager, which includes its headers.
$(PAGER_OBJ): | $(LIONS_LIBC)/include
ifeq ($(RAM_FATFS),1)
RAMFS_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIONSOS)/components/fs/ramfs/ramfs.mk
RAMFS_LIB := ramfs.a
RAMFS_IMAGE := ramfs.cpio
META_RAMFS_ARGS := --ramfs-image $(RAMFS_IMAGE) --ramfs-size $(RAMFS_SIZE)
endif

${IMAGES}: $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a $(BENCHMARK_LIBS)

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

client.o: %.o: $(TOP)/src/%.c | $(LIONS_LIBC)/include
	$(CC) -c $(CFLAGS) -I. -I$(RAMFS_CONFIG_DIR) $(BENCHMARK_INCLUDES) $(BENCHMARK_DEFINES) $< -o $@

client.elf: client.o libsddf_util_debug.a $(BENCHMARK_LIBS) $(RAMFS_LIB)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

ifeq ($(RAM_FATFS),1)
# io.c unpacks this into a FAT volume when the client mounts it. Entry names
# must not start with "./". cpio names its entries relative to a single
# directory, so the input trees are staged into one first; a missing root is a
# warning rather than an error, and no roots at all gives an empty file system.
$(RAMFS_IMAGE): $(shell find $(RAMFS_ROOTS) 2>/dev/null)
	rm -rf $(RAMFS_STAGE)
	mkdir -p $(RAMFS_STAGE)
	for root in $(RAMFS_ROOTS); do \
		if [ -d "$$root" ]; then \
			cp -R "$$root"/. $(RAMFS_STAGE)/; \
		else \
			echo "WARNING: ramfs input $$root not found, skipping"; \
		fi; \
	done
	$(foreach e,$(RAMFS_EXCLUDE),rm -rf $(RAMFS_STAGE)/$(e);)
	(cd $(RAMFS_STAGE) && find . -mindepth 1 -printf '%P\n' | LC_ALL=C sort | cpio -o -H newc --quiet) > $@

clean::
	rm -rf $(RAMFS_STAGE)
endif

-include client.d

.PHONY: $(BENCHMARK_LIBS)
# The benchmarks include libc headers, which only exist once musl is installed.
# One `<nnn>.a` rule per entry in SPEC_BENCHMARKS, each re-invoking that
# benchmark's own snippet, which writes its archive into $(BUILD_DIR). CPU is
# whatever board.mk picked for MICROKIT_BOARD.
define spec_benchmark_rule
$(call spec_num,$(1)).a: | $$(LIONS_LIBC)/include
	$$(MAKE) -C $$(BENCHMARKS_DIR)/$(1)/src -f $(call spec_num,$(1))-lions.mk \
		CPU=$$(CPU) LIONS_LIBC=$$(LIONS_LIBC)
endef
$(foreach b,$(SPEC_BENCHMARKS),$(eval $(call spec_benchmark_rule,$(b))))

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB) $(RAMFS_CONFIG_DIR)/fat_config.h $(RAMFS_IMAGE)
	PYTHONPATH=${SDDF}/tools/meta:$$PYTHONPATH \
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE) $(META_RAMFS_ARGS)

	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_client.data client.elf
	$(OBJCOPY) --update-section .pager_server_config=pager_server_pager.data pager.elf
	$(OBJCOPY) --update-section .pager_client_config=pager_client_client.data client.elf

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

# QEMU_ARCH_ARGS is sddf's description of the qemu machine for this board; QEMU
# is only set for a board that has one, so an empty QEMU means `make qemu` was
# asked for on real hardware.
qemu: ${IMAGE_FILE}
ifeq ($(strip $(QEMU)),)
	$(error make qemu is not supported for MICROKIT_BOARD=$(MICROKIT_BOARD))
else
	$(QEMU) $(QEMU_ARCH_ARGS) -nographic -d guest_errors
endif
