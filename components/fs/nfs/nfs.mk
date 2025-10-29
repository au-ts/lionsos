#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This Makefile snippet builds the NFS component
# it should be included into your project Makefile
#
# NOTES:
# Generates nfs.elf
# Requires ${SDDF}/util/util.mk to build the utility library for debug output

NFS_DIR := $(LIONSOS)/components/fs/nfs
LIBNFS := $(LIONSOS)/dep/libnfs

CFLAGS_nfs := \
	-I$(NFS_DIR)/lwip_include \
	-I$(LIBNFS)/include \
	-I$(LWIP)/include \
	-I$(LWIP)/include/ipv4 \
	-Wno-tautological-constant-out-of-range-compare

LIB_SDDF_LWIP_CFLAGS_nfs := ${CFLAGS_nfs}

NFS_FILES := nfs.c op.c
NFS_OBJ := $(addprefix nfs/, $(NFS_FILES:.c=.o))

CHECK_NFS_FLAGS_MD5 := .nfs_cflags-$(shell echo -- $(CFLAGS) $(CFLAGS_nfs) | shasum | sed 's/ *-//')

$(CHECK_NFS_FLAGS_MD5):
	-rm -f .nfs_cflags-*
	touch $@

$(LIBNFS)/CMakeLists.txt $(LIBNFS)/include &:
	cd $(LIONSOS); git submodule update --init dep/libnfs

libnfs/lib/libnfs.a: $(LIBNFS)/CMakeLists.txt | $(LIONS_LIBC)/include
	LIBNFS_LIBC=$(LIONS_LIBC) CC=$(CC) CPU=$(CPU) TARGET=$(TARGET) \
		cmake -S $(LIBNFS) -B libnfs \
		-DCMAKE_TOOLCHAIN_FILE=$(NFS_DIR)/toolchain.cmake \
		-DBUILD_SHARED_LIBS=OFF
	cmake --build libnfs

LIB_FS_SERVER_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIONSOS)/lib/fs/server/lib_fs_server.mk

nfs.elf: $(NFS_OBJ) libnfs/lib/libnfs.a lib_fs_server.a lib_sddf_lwip_nfs.a
	$(LD) $(LDFLAGS) -o $@ $(LIBS) $^

nfs:
	mkdir -p $@

$(NFS_OBJ): $(CHECK_NFS_FLAGS_MD5)
$(NFS_OBJ): $(LIBNFS)/include
$(NFS_OBJ): nfs
$(NFS_OBJ): CFLAGS := $(CFLAGS_nfs) \
		      $(CFLAGS)

nfs/%.o: $(NFS_DIR)/%.c | $(LIONS_LIBC)/include
	$(CC) -c $(CFLAGS) $< -o $@

-include $(NFS_OBJ:.o=.d)
