# File system makefile
# Not working due to the previous modification
FF_DIR := ../../fs/fat/

FatFs_OBJS :=   ff.o \
				ffsystem.o \
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

CFLAGS_T := \
	-mstrict-align \
	-ffreestanding \
	-g \
	-O0 \
	-Wall \
	-Wno-unused-function \
	-target aarch64-none-elf \
	-no-integrated-as \

$(BUILD_DIR)/musllibc/lib/libc.a:
	make -C $(MUSL) \
		C_COMPILER=$(CC) \
		TOOLPREFIX="" \
		CONFIG_ARCH_AARCH64=y \
		CONFIG_USER_DEBUG_BUILD=y \
		STAGE_DIR=$(abspath $(BUILD_DIR)/musllibc) \
		CFLAGS="-mstrict-align -ffreestanding -g -O0 -Wall -Wno-unused-function -target aarch64-none-elf -no-integrated-as" \
		SOURCE_DIR=.

$(BUILD_DIR)/%.o: $(FF_DIR)/ff15/source/%.c $(BUILD_DIR)/musllibc/lib/libc.a Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(FF_DIR)/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(FF_DIR)/FiberPool/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(FF_DIR)/FiberPool/FiberFlow/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/fs_shared_queue.o: $(FF_DIR)/libfssharedqueue/fs_shared_queue.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/FatFs.elf: $(addprefix $(BUILD_DIR)/, $(FatFs_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@
# File system building end