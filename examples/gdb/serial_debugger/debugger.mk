#
# Copyright 2024, UNSW (ABN 57 195 873 179)
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
    $(error libGDB requires a MICROKIT_SDK)
endif

ifeq ($(strip $(BOARD)),)
    $(error libGDB requires a BOARD)
endif

ifeq ($(strip $(BUILD_DIR)),)
    $(error libGDB requires a BUILD_DIR)
endif

ifeq ($(strip $(MICROKIT_CONFIG)),)
    $(error libGDB requires a MICROKIT_CONFIG)
endif

CFLAGS += -I$(MICROKIT_SDK)/board/$(BOARD)/$(MICROKIT_CONFIG)/include \
LDFLAGS += $(BUILD_DIR)/nfs/nfs.a

CFILES := debugger.c
OFILES := $(SRCS:.c=.o)

NAME := $(BUILD_DIR)/debugger.elf

$(NAME): $(BUILD_DIR)/debugger.o $(BUILD_DIR)/libgdb.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@
