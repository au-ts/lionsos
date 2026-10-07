# Shared by the pager examples, which run this from their build directory and
# link the resulting minor_pf.a into their client.
#
# Requires variables:
#	LIONSOS
#	LIONS_LIBC
#	BUILD_DIR, exported by the example's Makefile

ifeq ($(strip $(BUILD_DIR)),)
$(error BUILD_DIR must be specified)
endif

# Variables
CC = clang
CFLAGS = --target=aarch64-none-elf -Wall -Wextra -Iinclude \
-I${LIONSOS}/lib/libc/include \
-I$(LIONS_LIBC)/include \
-I$(BUILD_DIR)/libc/include \
-I${LIONSOS}/components/pager/config \
-O3 -mcpu=cortex-a53 -MD
LIB_NAME = minor_pf.a

# Directories
SRC_DIR = .
OBJ_DIR = $(BUILD_DIR)/obj

# The final path for the library
LIB_TARGET = $(BUILD_DIR)/$(LIB_NAME)

# Files
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# Default target
all: $(LIB_TARGET)

# Create the static library using 'ar' (archiver)
$(LIB_TARGET): $(OBJS) | $(BUILD_DIR)
	ar rcs $@ $^

# Rebuild when the flags change, the instrumentation switch being one of them.
CHECK_FLAGS_MD5 := $(OBJ_DIR)/.minor_pf_cflags-$(shell echo -- $(CFLAGS) | shasum | sed 's/ *-//')

$(CHECK_FLAGS_MD5): | $(OBJ_DIR)
	-rm -f $(OBJ_DIR)/.minor_pf_cflags-*
	touch $@

# Compile source files into object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(CHECK_FLAGS_MD5) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Create directories if they don't exist
$(BUILD_DIR) $(OBJ_DIR):
	mkdir -p $@

clean::
	rm -rf $(OBJ_DIR) $(LIB_TARGET)

.PHONY: all clean

-include $(OBJS:.o=.d)
