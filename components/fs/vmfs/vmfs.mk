#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This Makefile snippet builds filesystem UIO driver
#

ifeq ($(strip $(LIBVMM_TOOLS)),)
$(error LIBVMM_TOOLS must be specified)
endif
ifeq ($(strip $(SDDF)),)
$(error SDDF must be specified)
endif
ifeq ($(strip $(CC_USERLEVEL)),)
$(error CC_USERLEVEL must be specified)
endif

fs_driver_init: $(LIBVMM_DIR)/tools/linux/fs/board/$(MICROKIT_BOARD)/fs_driver_init
	cp $^ $@

UIO_FS_IMAGES_DEP := uio_fs_driver_main.o uio_fs_driver_op.o uio_fs_driver_util.o liburing.a
UIO_FS_IMAGES := uio_fs_driver

CFLAGS_uio_fs_driver := -I$(SDDF)/include -I$(LIBVMM_TOOLS)/linux/include

CHECK_UIO_FS_DRIVER_FLAGS_MD5:=.uio_fs_driver_cflags-$(shell echo -- $(CFLAGS_USERLEVEL) $(CFLAGS_uio_fs_driver) | shasum | sed 's/ *-//')

$(CHECK_UIO_FS_DRIVER_FLAGS_MD5):
	-rm -f .uio_fs_driver_cflags-*
	touch $@

# Compile liburing for io_uring operations
LIBURING := $(LIBVMM_TOOLS)/../dep/liburing
# zig cc and zig c++ does not have elf.h on macos
LIBURING_CC := aarch64-linux-gnu-gcc
LIBURING_CPP := aarch64-linux-gnu-g++

CFLAGS_uio_fs_driver := $(CFLAGS_uio_fs_driver) -Iliburing/src/include

liburing.a:
	cp -r $(LIBURING) liburing && \
	cd liburing && \
	./configure --cc=$(LIBURING_CC) --cxx=$(LIBURING_CPP) --includedir=$(LIBVMM_TOOLS)/linux/uio_drivers/fs/linux && \
	make library && \
	cp src/liburing.a ../liburing.a

# Compile our FS UIO driver
uio_fs_driver: $(UIO_FS_IMAGES_DEP)
	$(CC_USERLEVEL) -static $(CFLAGS_USERLEVEL) $(CFLAGS_uio_fs_driver) $^ -o $@

# uio_fs_driver_main.o: $(CHECK_UIO_FS_DRIVER_FLAGS_MD5)
uio_fs_driver_main.o: $(LIBVMM_TOOLS)/linux/uio_drivers/fs/main.c $(LIBVMM_TOOLS)/linux/uio_drivers/fs/log.h liburing.a
	$(CC_USERLEVEL) $(CFLAGS_USERLEVEL) $(CFLAGS_uio_fs_driver) -o $@ -c $<

# uio_fs_driver_op.o: $(CHECK_UIO_FS_DRIVER_FLAGS_MD5)
uio_fs_driver_op.o: $(LIBVMM_TOOLS)/linux/uio_drivers/fs/op.c $(LIBVMM_TOOLS)/linux/uio_drivers/fs/log.h liburing.a
	$(CC_USERLEVEL) $(CFLAGS_USERLEVEL) $(CFLAGS_uio_fs_driver) -o $@ -c $<

# uio_fs_driver_util.o: $(CHECK_UIO_FS_DRIVER_FLAGS_MD5)
uio_fs_driver_util.o: $(LIBVMM_TOOLS)/linux/uio_drivers/fs/util.c $(LIBVMM_TOOLS)/linux/uio_drivers/fs/log.h liburing.a
	$(CC_USERLEVEL) $(CFLAGS_USERLEVEL) $(CFLAGS_uio_fs_driver) -o $@ -c $<

clean::
	rm -f uio_fs_driver.[od] .uio_fs_driver_cflags-*

clobber::
	rm -f $(UIO_FS_IMAGES)

-include uio_fs_driver.d
