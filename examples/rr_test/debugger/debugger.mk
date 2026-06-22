#
# Copyright 2024, UNSW (ABN 57 195 873 179)
#
# SPDX-License-Identifier: BSD-2-Clause
#

CFILES := debugger.c
OFILES := $(CFILES:.c=.o)

debugger.o: $(RR_TEST_DIR)/debugger/debugger.c
	$(CC) $(CFLAGS) -c -o $@ $^

debugger.elf: debugger.o libgdb.a libsddf_util.a libco.a
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

