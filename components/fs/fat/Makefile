# File system makefile
FF_DIR := ./

BUILD_DIR ?= build

CC = ${C_COMPILER}
LD := $(LINKER)
CFLAGS = $(FS_BUILD_FLAGS)

BOARD_DIR := $(MICROKIT_SDK)/board/$(BOARD)/$(CONFIG)
LDFLAGS := -L$(BOARD_DIR)/lib

LIONSOS ?= ../../../
MUSL = $(LIONSOS)/dep/musllibc

FatFs_OBJS :=   ff.o \
				ffunicode.o \
                fatfs_event.o \
				fatfs_op.o \
				fs_diskio.o \
				FiberPool.o \
				FiberFlow.o \
				printf.o \
				util.o \

$(BUILD_DIR)/musllibc/lib/libc.a:
	make -C $(MUSL) \
		C_COMPILER=aarch64-none-elf-gcc \
		TOOLPREFIX=aarch64-none-elf- \
		CONFIG_ARCH_AARCH64=y \
		STAGE_DIR=$(abspath $(BUILD_DIR)/musllibc) \
		SOURCE_DIR=.

$(BUILD_DIR)/ff.o: $(FF_DIR)/ff15/source/ff.c $(BUILD_DIR)/musllibc/lib/libc.a Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/ffunicode.o: $(FF_DIR)/ff15/source/ffunicode.c $(BUILD_DIR)/musllibc/lib/libc.a Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/fatfs_event.o: $(FF_DIR)/fatfs_event.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/fatfs_op.o: $(FF_DIR)/fatfs_op.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/fs_diskio.o: $(FF_DIR)/fs_diskio.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(FF_DIR)/FiberPool/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(FF_DIR)/FiberPool/FiberFlow/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/fatFs.elf: $(addprefix $(BUILD_DIR)/, $(FatFs_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@
# File system building end