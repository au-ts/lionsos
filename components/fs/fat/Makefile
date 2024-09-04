# File system makefile
FS_DIR := .

LIONSOS := ../../..

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(CONFIG)

EXEC := $(BUILD_DIR)/fatfs.elf

FATFS_OBJECT_DIR := $(BUILD_DIR)/fatfs

all: $(EXEC)

# Compiler flags
CFLAGS := -Wall \
          -Wextra \
		  -target $(TARGET) \
		  -ffreestanding \
		  -g \
		  -O0 \
		  -mstrict-align \
		  -I$(LIBC_DIR)/include \
		  -I$(BOARD_DIR)/include \
		  -I$(LIONSOS)/dep \
		  -I$(LIONSOS)/include/lions \
          -I$(LIONSOS)/dep/sddf/include \
		  -I$(FS_DIR) \
		  -I$(EXAMPLE_SRC_DIR)

LDFLAGS := -L$(LIBC_DIR)/lib -lc -L$(FATFS_OBJECT_DIR)/libmicrokitco -lmicrokitco

LIBS := -lmicrokit -Tmicrokit.ld -L$(BOARD_DIR)/lib

FATFS_OBJS :=   ff.o \
				ffunicode.o \
                fatfs_event.o \
				fatfs_op.o \
				co_helper.o \
				fs_diskio.o \
				printf.o \
				putchar_debug.o

LIBMICROKITCO_OBJ := libmicrokitco/libmicrokitco.a
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco
LIBMICROKITCO_OPT_PATH := $(LIONSOS)/components/fs/fat

COROUTINE_OBJS := $(FATFS_OBJECT_DIR)/$(LIBMICROKITCO_OBJ)
CPPFLAGS := -DUSE_LIBMICROKITCO

$(FATFS_OBJECT_DIR)/$(LIBMICROKITCO_OBJ):
	make -f $(LIBMICROKITCO_PATH)/Makefile \
			LIBMICROKITCO_PATH=$(LIBMICROKITCO_PATH) \
			TARGET=$(TARGET) \
			MICROKIT_SDK:=$(MICROKIT_SDK) \
			BUILD_DIR:=$(FATFS_OBJECT_DIR) \
			MICROKIT_BOARD:=$(MICROKIT_BOARD) \
			MICROKIT_CONFIG:=$(CONFIG) \
			CPU:=$(CPU) \
			LLVM:=1 \
			LIBMICROKITCO_OPT_PATH=$(LIBMICROKITCO_OPT_PATH)

$(FATFS_OBJECT_DIR):
	mkdir -p $(FATFS_OBJECT_DIR)

$(FATFS_OBJECT_DIR)/ff.o: $(FS_DIR)/ff15/source/ff.c Makefile | $(FATFS_OBJECT_DIR)
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

$(FATFS_OBJECT_DIR)/ffunicode.o: $(FS_DIR)/ff15/source/ffunicode.c Makefile
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

$(FATFS_OBJECT_DIR)/fatfs_event.o: $(FS_DIR)/fatfs_event.c $(COROUTINE_OBJS) Makefile
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

$(FATFS_OBJECT_DIR)/fatfs_op.o: $(FS_DIR)/fatfs_op.c Makefile
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

$(FATFS_OBJECT_DIR)/fs_diskio.o: $(FS_DIR)/fs_diskio.c Makefile
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

$(FATFS_OBJECT_DIR)/co_helper.o: $(FS_DIR)/co_helper.c Makefile
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

$(FATFS_OBJECT_DIR)/printf.o: $(LIONSOS)/dep/sddf/util/printf.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(FATFS_OBJECT_DIR)/putchar_debug.o: $(LIONSOS)/dep/sddf/util/putchar_debug.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/fatfs.elf: $(addprefix $(FATFS_OBJECT_DIR)/, $(FATFS_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@
# File system building end