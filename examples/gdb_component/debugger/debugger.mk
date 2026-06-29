#
# Copyright 2024, UNSW (ABN 57 195 873 179)
#
# SPDX-License-Identifier: BSD-2-Clause
#

CFILES := debugger.c
OFILES := $(CFILES:.c=.o)

debugger.o: $(GDB_COMPONENT_DIR)/debugger/debugger.c | $(LIONS_LIBC)/lib/libc.a
	$(CC) $(CFLAGS) -O0 -c -o $@ $^

debugger.elf: debugger.o libgdb.a
	${LD} ${LDFLAGS} -O0 -o $@ $^ ${LIBS}

