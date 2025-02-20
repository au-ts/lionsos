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
LWIP := $(SDDF)/network/ipstacks/lwip/src
LIBNFS := $(LIONSOS)/dep/libnfs

CFLAGS_nfs := \
	-I$(MUSL)/include \
	-I$(NFS_DIR)/lwip_include \
	-I$(LIBNFS)/include \
	-I$(LWIP)/include \
	-I$(LWIP)/include/ipv4 \

include $(LWIP)/Filelists.mk
$(LWIP)/Filelists.mk:
	cd $(LIONSOS); git submodule update --init $(SDDF)

NFS_NETIFFILES := $(LWIPDIR)/netif/ethernet.c
NFS_LWIPFILES := $(COREFILES) $(CORE4FILES) $(NFS_NETIFFILES)
NFS_LWIP_OBJ := $(addprefix nfs/lwip/, $(NFS_LWIPFILES:.c=.o))

NFS_DIRS := nfs $(addprefix nfs/lwip/, api core core/ipv4 netif)

NFS_FILES := nfs.c op.c posix.c tcp.c
NFS_OBJ := $(addprefix nfs/, $(NFS_FILES:.c=.o)) $(NFS_LWIP_OBJ)

CHECK_NFS_FLAGS_MD5 := .nfs_cflags-$(shell echo -- $(CFLAGS) $(CFLAGS_nfs) | shasum | sed 's/ *-//')

$(CHECK_NFS_FLAGS_MD5):
	-rm -f .nfs_cflags-*
	touch $@

$(LIBNFS)/CMakeLists.txt $(LIBNFS)/include:
	cd $(LIONSOS); git submodule update --init dep/libnfs

libnfs/lib/libnfs.a: $(LIBNFS)/CMakeLists.txt $(MUSL)/lib/libc.a
	MUSL=$(abspath $(MUSL)) cmake -S $(LIBNFS) -B libnfs
	cmake --build libnfs

LIB_FS_SERVER_LIBC_INCLUDE := $(MUSL)/include
include $(LIONSOS)/lib/fs/server/lib_fs_server.mk

nfs.elf: LDFLAGS += -L$(LIBGCC)
nfs.elf: LIBS += -lgcc
nfs.elf: $(NFS_OBJ) $(MUSL)/lib/libc.a libnfs/lib/libnfs.a lib_fs_server.a
	$(LD) $(LDFLAGS) -o $@ $(LIBS) $^

$(NFS_DIRS):
	mkdir -p $@

$(NFS_OBJ): $(CHECK_NFS_FLAGS_MD5)
$(NFS_OBJ): $(MUSL)/lib/libc.a
$(NFS_OBJ): $(LIBNFS)/include
$(NFS_OBJ): |$(NFS_DIRS)
$(NFS_OBJ): CFLAGS += $(CFLAGS_nfs)

nfs/lwip/%.o: $(LWIP)/%.c
	$(CC) -c $(CFLAGS) $< -o $@

nfs/%.o: $(NFS_DIR)/%.c
	$(CC) -c $(CFLAGS) $< -o $@

-include $(NFS_OBJ:.o=.d)
