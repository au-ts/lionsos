# Path to micropython source code
MICROPYTHON_SRC := ../../../../micropython

# Include the core environment definitions; this will set $(TOP).
include $(MICROPYTHON_SRC)/py/mkenv.mk

# Include py core make definitions.
include $(TOP)/py/py.mk
include $(TOP)/extmod/extmod.mk

CROSS_COMPILE ?= aarch64-none-elf-

CPU ?= cortex-a55
BOARD_DIR := $(SEL4CP_SDK)/board/$(SEL4CP_BOARD)/$(SEL4CP_CONFIG)

# Set CFLAGS and libraries.
CFLAGS += -I. -I$(BUILD) -I$(TOP) -I$(BOARD_DIR)/include -nostdlib -ffreestanding -mtune=$(CPU)
LDFLAGS += -L$(BOARD_DIR)/lib -nostdlib
LIBS += -lm -lsel4cp -Tsel4cp.ld

# Define the required source files.
SRC_C = \
	micropython.c \
	uart.c \
	mphalport.c \
	modmyport.c \
	shared/readline/readline.c \
	shared/runtime/gchelper_generic.c \
	shared/runtime/pyexec.c \
	shared/runtime/stdout_helpers.c \
	shared/libc/string0.c \
	shared/libc/printf.c \
	shared/libc/__errno.c \

# Define source files containung qstrs.
SRC_QSTR += shared/readline/readline.c shared/runtime/pyexec.c modmyport.c

# Define the required object files.
OBJ = $(PY_CORE_O) $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

all: $(BUILD)/micropython.elf

$(BUILD)/micropython.elf: $(OBJ)
	$(ECHO) "LINK $@"
	$(Q)$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
	$(Q)$(SIZE) $@

# Include remaining core make rules.
include $(TOP)/py/mkrules.mk
