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

FatFs_OBJS :=   ff.o \z
				ffunicode.o \
                AsyncFATFs.o \
				AsyncFATFunc.o \
				Asyncdiskio.o \
				fs_shared_queue.o \
				sddf_blk_shared_queue.o \
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

$(BUILD_DIR)/AsyncFATFs.o: $(FF_DIR)/AsyncFATFs.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/AsyncFATFunc.o: $(FF_DIR)/AsyncFATFunc.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/Asyncdiskio.o: $(FF_DIR)/Asyncdiskio.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(FF_DIR)/FiberPool/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(FF_DIR)/FiberPool/FiberFlow/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/FatFs.elf: $(addprefix $(BUILD_DIR)/, $(FatFs_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@
# File system building end