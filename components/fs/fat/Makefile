# make MICROKIT_SDK=/home/li/Sel4/microkit-sdk-1.2.6 BOARD=odroidc4 USE_LIBMICROKITCO=1
# make MICROKIT_SDK=/home/li/Sel4/microkit-sdk-1.2.6 BOARD=odroidc4 USE_FIBERPOOL=1
# File system makefile
FS_DIR := ./

BUILD_DIR ?= ./build

CC = clang
LD := ld.lld
CONFIG := debug
CPU := cortex-a55

LIONSOS := ../../..

BOARD_DIR := $(MICROKIT_SDK)/board/$(BOARD)/$(CONFIG)

TARGET := aarch64-none-elf

EXEC := fatfs.elf

all: $(EXEC)

BOARD_DIR := $(MICROKIT_SDK)/board/$(BOARD)/$(CONFIG)

LIBC_DIR ?= ./libc

# Compiler flags
CFLAGS ?= -Wall \
          -Wextra \
		  -target $(TARGET) \
		  -ffreestanding \
		  -g \
		  -O0 \
		  -mstrict-align \
		  -I./FiberPool \
		  -I$(LIBC_DIR)/include \
		  -I../../../dep/sddf/include/ \
		  -I$(BOARD_DIR)/include \
		  -I$(FS_DIR)

LDFLAGS := -L./libc/lib -lc

LIBS := -lmicrokit -Tmicrokit.ld -L$(BOARD_DIR)/lib

FatFs_OBJS :=   ff.o \
				ffunicode.o \
                fatfs_event.o \
				fatfs_op.o \
				co_helper.o \
				fs_diskio.o \
				printf.o \
				putchar_debug.o

FIBERPOOL_OBJ := fiberpool/libfiberpool.a
FIBERPOOL_PATH := $(LIONSOS)/dep/libmicrokitco

ifdef USE_FIBERPOOL
LDFLAGS += -L$(BUILD_DIR)/fiberpool -lfiberpool
COROUTINE_OBJS := $(BUILD_DIR)/$(FIBERPOOL_OBJ)
CPPFLAGS := -DUSE_FIBERPOOL
endif

LIBMICROKITCO_OBJ := libmicrokitco/libmicrokitco.a
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco
LIBMICROKITCO_OPT_PATH := $(LIONSOS)/components/fs/fat

ifdef USE_LIBMICROKITCO
LDFLAGS += -L$(BUILD_DIR)/libmicrokitco -lmicrokitco
COROUTINE_OBJS := $(BUILD_DIR)/$(LIBMICROKITCO_OBJ)
CPPFLAGS := -DUSE_LIBMICROKITCO
endif

$(BUILD_DIR)/$(LIBMICROKITCO_OBJ):
	make -f $(LIBMICROKITCO_PATH)/Makefile \
			LIBMICROKITCO_PATH=$(LIBMICROKITCO_PATH) \
			TARGET=$(TARGET) \
			MICROKIT_SDK:=$(MICROKIT_SDK) \
			BUILD_DIR:=$(BUILD_DIR) \
			MICROKIT_BOARD:=$(BOARD) \
			MICROKIT_CONFIG:=$(CONFIG) \
			CPU:=$(CPU) \
			LLVM:=1 \
			LIBMICROKITCO_OPT_PATH=$(LIBMICROKITCO_OPT_PATH)

$(BUILD_DIR)/ff.o: $(FS_DIR)/ff15/source/ff.c Makefile
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

$(BUILD_DIR)/ffunicode.o: $(FS_DIR)/ff15/source/ffunicode.c Makefile
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

$(BUILD_DIR)/fatfs_event.o: $(FS_DIR)/fatfs_event.c $(COROUTINE_OBJS) Makefile
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

$(BUILD_DIR)/fatfs_op.o: $(FS_DIR)/fatfs_op.c Makefile
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

$(BUILD_DIR)/fs_diskio.o: $(FS_DIR)/fs_diskio.c Makefile
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

$(BUILD_DIR)/co_helper.o: $(FS_DIR)/co_helper.c Makefile
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

$(BUILD_DIR)/printf.o: $(LIONSOS)/dep/sddf/util/printf.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/putchar_debug.o: $(LIONSOS)/dep/sddf/util/putchar_debug.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

fatfs.elf: $(addprefix $(BUILD_DIR)/, $(FatFs_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@
# File system building end