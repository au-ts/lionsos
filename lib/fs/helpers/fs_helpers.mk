#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This Makefile snippet builds the FS helpers library
#

$(BUILD)/fs:
	mkdir -p $@

$(BUILD)/fs/helpers.o: $(LIONSOS)/lib/fs/helpers/helpers.c | $(BUILD)/fs $(LIONS_LIBC)/include
	${CC} ${CFLAGS} -c -o $@ $<
