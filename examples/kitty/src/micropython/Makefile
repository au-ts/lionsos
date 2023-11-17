LIONSOS := ../../../..
SDDF := $(LIONSOS)/sddf
LIBCO := $(LIONSOS)/libco
# Path to micropython source code
MICROPYTHON_SRC := $(LIONSOS)/micropython

# Include the core environment definitions; this will set $(TOP).
include $(MICROPYTHON_SRC)/py/mkenv.mk

# Include py core make definitions.
include $(TOP)/py/py.mk
include $(TOP)/extmod/extmod.mk

CROSS_COMPILE ?= aarch64-none-elf-

CPU ?= cortex-a55
BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)

# Set CFLAGS and libraries.
CFLAGS += -I. -I$(BUILD) -I$(TOP) -I$(SDDF) -I$(SDDF)/include -I$(BOARD_DIR)/include -I$(LIBCO) -I$(LIONSOS)/fs/include -nostdlib -ffreestanding -mtune=$(CPU)
LDFLAGS += -L$(BOARD_DIR)/lib -nostdlib
LIBS += -lm -lmicrokit -Tmicrokit.ld

# Define the required source files.
SRC_C = \
	micropython.c \
	uart.c \
	mphalport.c \
	modmyport.c \
	modtime.c \
	modfb.c \
	vfs_sddf_fs_file.c \
	vfs_sddf_fs.c \
	sddf_fs.c \
	extmod/modtime.c \
	extmod/vfs.c \
	extmod/modos.c \
	extmod/vfs_reader.c \
	shared/readline/readline.c \
	shared/runtime/gchelper_generic.c \
	shared/runtime/pyexec.c \
	shared/runtime/stdout_helpers.c \
	shared/libc/string0.c \
	shared/libc/printf.c \
	shared/libc/__errno.c \
	$(LIBCO)/libco.c

# Define source files containung qstrs.
SRC_QSTR += shared/readline/readline.c shared/runtime/pyexec.c modmyport.c modfb.c vfs_sddf_fs_file.c vfs_sddf_fs.c

# Define the required object files.
OBJ = $(PY_CORE_O) $(addprefix $(BUILD)/, $(SRC_C:.c=.o)) ../../build/sddf_timer_client.o

all: $(BUILD)/micropython.elf

$(BUILD)/micropython.elf: $(OBJ)
	$(ECHO) "LINK $@"
	$(Q)$(CC) $(LDFLAGS) -o $@ $(BUILD)/$(PACKAGE_PYTHON_SCRIPTS) $^ $(LIBS)
	$(Q)$(SIZE) $@

# Include remaining core make rules.
include $(TOP)/py/mkrules.mk
