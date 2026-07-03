#
# Copyright 2024, UNSW (ABN 57 195 873 179)
#
# SPDX-License-Identifier: BSD-2-Clause
#

CFILES := debugger.c libgdbcomp.c
OFILES := $(CFILES:.c=.o)

libgdbcomp.o: $(GDB_COMPONENT_DIR)/debugger/libgdbcomp.c | $(LIONS_LIBC)/lib/libc.a
	$(CC) $(CFLAGS) -c -o $@ $^

debugger.o: $(GDB_COMPONENT_DIR)/debugger/debugger.c | $(LIONS_LIBC)/lib/libc.a
	$(CC) $(CFLAGS) -c -o $@ $^

debugger.elf: debugger.o libgdb.a libco.a libvspace.a libgdbcomp.o | $(LIONS_LIBC)/lib/libc.a
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

