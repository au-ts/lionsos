#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
override MICROKIT_SDK:=$(abspath ${MICROKIT_SDK})

export LIONSOS ?= $(abspath ../..)
export ARINC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export MICROKIT_CONFIG ?= debug
export BUILD_DIR ?= $(abspath build)

# PARTITION COMPONENT DIRECTORIES
PART_1_SRC_DIR := $(ARINC_DIR)/p1
PART_2_SRC_DIR := $(ARINC_DIR)/p2
PART_3_SRC_DIR := $(ARINC_DIR)/p3
export PART_SRC_VPATH := $(PART_1_SRC_DIR) $(PART_2_SRC_DIR) $(PART_3_SRC_DIR)

# PARTITION ELFs
PART_1_ELFS := p1_spd.elf p1_upd.elf
PART_2_ELFS := p2_spd.elf p2_upd.elf
PART_3_ELFS := p3_spd.elf p3_upd.elf

export PART_1_UPD_OBJS := p1_upd.o sb_queue_int32_t_1.o
export PART_2_UPD_OBJS := p2_upd.o sb_queue_int32_t_1.o
export PART_3_UPD_OBJS := p3_upd.o sb_queue_int32_t_1.o

export PART_ELFS := $(PART_1_ELFS) $(PART_2_ELFS) $(PART_3_ELFS)

IMAGE_FILE := $(BUILD_DIR)/arinc_scheduling.img
REPORT_FILE := $(BUILD_DIR)/report.txt

all: ${IMAGE_FILE}

qemu ${IMAGE_FILE} ${REPORT_FILE} clean clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: arinc_scheduling.mk
	mkdir -p ${BUILD_DIR}
	cp arinc_scheduling.mk $@

FORCE:
