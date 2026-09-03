
# This Makefile builds the ramfs component

# NOTES:
# Requires variables:
#   LIONSOS
#   TARGET
#   MICROKIT_SDK
#   MICROKIT_BOARD
#   MICROKIT_CONFIG
#   CPU
#   RAMFS_LIBC_INCLUDE
#   RAMFS_LIBC_LIB
# Generates ramfs.elf


RAMFS_SRC_DIR := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))


RAMFS_CFLAGS := \
   -I$(RAMFS_LIBC_INCLUDE) \
   -I$(LIBMICROKITCO_PATH) \
   -I$(RAMFS_SRC_DIR)/config


LIBMICROKITCO_CFLAGS_ramfs := ${RAMFS_CFLAGS}


RAMFS_OBJ := \
   op.o \
   io.o


CHECK_RAMFS_FLAGS_MD5 := .ramfs_cflags-$(shell echo -- $(CFLAGS) $(RAMFS_CFLAGS) | shasum | sed 's/ *-//')


$(CHECK_RAMFS_FLAGS_MD5):
   -rm -f .ramfs_cflags-*
   touch $@


LIB_FS_SERVER_LIBC_INCLUDE := $(RAMFS_LIBC_INCLUDE)
include $(LIONSOS)/lib/fs/server/lib_fs_server.mk


ramfs:
   mkdir -p ramfs


ramfs/%.o: CFLAGS += $(RAMFS_CFLAGS)
ramfs/%.o: $(RAMFS_SRC_DIR)/%.c $(RAMFS_LIBC_INCLUDE) $(CHECK_RAMFS_FLAGS_MD5) |ramfs
   $(CC) -c $(CFLAGS) $< -o $@


ramfs.elf: $(RAMFS_OBJ) libmicrokitco_fat.a $(RAMFS_LIBC_LIB) lib_fs_server.a
   $(LD) $(LDFLAGS) $^ $(LIBS) -o $@


-include $(RAMFS_OBJ:.o=.d)
