#
# Copyright 2026, LionsOS Contributors
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Include this snippet in your project Makefile to build the input
# components. It generates:
#   input_driver.elf  virtIO input driver, one image shared by all devices.
#                     Copy it per device and patch each copy's
#                     .input_driver_config section.
#   input_virt.elf    input virtualiser, which includes <input_config.h>
#                     from the project's include path.
#
# Assumes libsddf_util_debug.a is in LIBS and that CFLAGS contains the
# LionsOS and sDDF include directories.

INPUT_COMPONENTS_DIR := $(LIONSOS)/components/input

input_driver.elf: input/virtio/input.o
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

input_virt.elf: input/virt.o
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

input/virtio/input.o: $(INPUT_COMPONENTS_DIR)/virtio/input.c | $(SDDF_LIBC_INCLUDE)
	mkdir -p input/virtio
	$(CC) -c $(CFLAGS) -I$(INPUT_COMPONENTS_DIR)/virtio -o $@ $<

input/virt.o: $(INPUT_COMPONENTS_DIR)/virt.c | $(SDDF_LIBC_INCLUDE)
	mkdir -p input
	$(CC) -c $(CFLAGS) -o $@ $<

-include input/virtio/input.d input/virt.d

clean::
	rm -rf input

clobber:: clean
	rm -f input_driver.elf input_virt.elf
