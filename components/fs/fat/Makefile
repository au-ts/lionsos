#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

FS_DIR := .

LIONSOS := ../../..

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(CONFIG)

EXEC := $(BUILD_DIR)/fat.elf

FAT_OBJECT_DIR := $(BUILD_DIR)/fat

FF15_SRC := $(LIONSOS)/dep/ff15

LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco

all: $(EXEC)

# Compiler flags
CFLAGS := \
	-Wall \
	-Wextra \
	-target $(TARGET) \
	-ffreestanding \
	-g \
	-mstrict-align \
	-I$(LIBC_DIR)/include \
	-I$(BOARD_DIR)/include \
	-I$(LIONSOS)/include \
	-I$(LIONSOS)/dep/sddf/include \
	-I$(LIBMICROKITCO_PATH) \
	-I$(FF15_SRC) \
	-I$(CONFIG_INCLUDE) \
	-I$(FS_DIR)/config

LDFLAGS := \
	-L$(LIBC_DIR)/lib \
	-L$(FAT_OBJECT_DIR)/libmicrokitco \
	-L$(BOARD_DIR)/lib \

LIBS := \
	-lmicrokit \
	-Tmicrokit.ld \
	-lc \
	-lmicrokitco

FAT_OBJS := \
	ff.o \
	ffunicode.o \
	event.o \
	op.o \
	io.o \
	printf.o \
	putchar_debug.o \
	assert.o

LIBMICROKITCO_OBJ := libmicrokitco/libmicrokitco.a
LIBMICROKITCO_OPT_PATH := $(LIONSOS)/components/fs/fat/config

LIBMICROKITCO_OBJS := $(FAT_OBJECT_DIR)/$(LIBMICROKITCO_OBJ)

$(FAT_OBJECT_DIR)/$(LIBMICROKITCO_OBJ):
	make -f $(LIBMICROKITCO_PATH)/Makefile \
			LIBMICROKITCO_PATH=$(LIBMICROKITCO_PATH) \
			TARGET=$(TARGET) \
			MICROKIT_SDK:=$(MICROKIT_SDK) \
			BUILD_DIR:=$(FAT_OBJECT_DIR) \
			MICROKIT_BOARD:=$(MICROKIT_BOARD) \
			MICROKIT_CONFIG:=$(CONFIG) \
			CPU:=$(CPU) \
			LLVM:=1 \
			LIBMICROKITCO_OPT_PATH=$(LIBMICROKITCO_OPT_PATH)

$(FAT_OBJECT_DIR):
	mkdir -p $(FAT_OBJECT_DIR)

$(FAT_OBJECT_DIR)/ff.o: $(FF15_SRC)/ff.c Makefile | $(FAT_OBJECT_DIR)
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

$(FAT_OBJECT_DIR)/ffunicode.o: $(FF15_SRC)/ffunicode.c Makefile
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

$(FAT_OBJECT_DIR)/event.o: $(FS_DIR)/event.c $(LIBMICROKITCO_OBJS) Makefile
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

$(FAT_OBJECT_DIR)/op.o: $(FS_DIR)/op.c Makefile
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

$(FAT_OBJECT_DIR)/io.o: $(FS_DIR)/io.c Makefile
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

$(FAT_OBJECT_DIR)/printf.o: $(LIONSOS)/dep/sddf/util/printf.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(FAT_OBJECT_DIR)/putchar_debug.o: $(LIONSOS)/dep/sddf/util/putchar_debug.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(FAT_OBJECT_DIR)/assert.o: $(LIONSOS)/dep/sddf/util/assert.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/fat.elf: $(addprefix $(FAT_OBJECT_DIR)/, $(FAT_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@
