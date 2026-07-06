#
# Copyright 2024, UNSW (ABN 57 195 873 179)
#
# SPDX-License-Identifier: BSD-2-Clause
#

DEBUGGER_DIR := $(LIONSOS)/components/debugger

CFILES := $(DEBUGGER_DIR)/debugger.c $(DEBUGGER_DIR)/libgdbcomp.c
OFILES := $(CFILES:.c=.o)

libgdbcomp.o: $(DEBUGGER_DIR)/libgdbcomp.c | $(LIONS_LIBC)/lib/libc.a
	$(CC) $(CFLAGS) -c -o $@ $^

debugger.o: $(DEBUGGER_DIR)/debugger.c | $(LIONS_LIBC)/lib/libc.a
	$(CC) $(CFLAGS) -c -o $@ $^

debugger.elf: debugger.o libgdb.a libco.a libvspace.a libgdbcomp.o | $(LIONS_LIBC)/lib/libc.a
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}
