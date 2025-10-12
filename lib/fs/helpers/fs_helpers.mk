#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This Makefile snippet builds the FS helpers library
#

$(BUILD)/fs/helpers.o: $(LIONSOS)/lib/fs/helpers/helpers.c
	${CC} ${CFLAGS} -c -o $@ $<
