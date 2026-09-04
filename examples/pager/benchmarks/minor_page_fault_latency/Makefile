# Variables
CC = clang
CFLAGS = -Wall -Wextra -Iinclude -O3 -mcpu=cortex-a53
LIB_NAME = minor_pf.a

# Directories
SRC_DIR = .
BUILD_DIR = ../../build
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

# Compile source files into object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Create directories if they don't exist
$(BUILD_DIR) $(OBJ_DIR):
	mkdir -p $@

clean::
	rm -rf $(OBJ_DIR) $(LIB_TARGET)

.PHONY: all clean