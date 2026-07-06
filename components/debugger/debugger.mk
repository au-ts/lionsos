#
# Copyright 2024, UNSW (ABN 57 195 873 179)
#
# SPDX-License-Identifier: BSD-2-Clause
#
DEBUGGER_SUPPORTED_BACKENDS := \
	serial \
	net

ifndef DEBUGGER_BACKEND
  $(error Please specify debugger backend. Supported backends: ${DEBUGGER_SUPPORTED_BACKENDS})
endif

ifeq ($(findstring $(strip ${DEBUGGER_BACKEND}), ${DEBUGGER_SUPPORTED_BACKENDS}),)
  $(error ${DEBUGGER_BACKEND} is not a valid backend. Supported backends: ${DEBUGGER_SUPPORTED_BACKENDS})
endif


DEBUGGER_DIR := $(LIONSOS)/components/debugger
_DEBUGGER_BACKEND_FILE := $(DEBUGGER_DIR)/backends/$(DEBUGGER_BACKEND).c
_DEBUGGER_CFILES := $(_DEBUGGER_BACKEND_FILE) $(DEBUGGER_DIR)/libgdbcomp.c
_DEBUGGER_OFILES := $(DEBUGGER_CFILES:.c=.o)

libgdbcomp.o: $(DEBUGGER_DIR)/libgdbcomp.c | $(LIONS_LIBC)/lib/libc.a
	$(CC) $(CFLAGS) -I $(DEBUGGER_DIR) -c -o $@ $^

debugger.o: $(_DEBUGGER_BACKEND_FILE) | $(LIONS_LIBC)/lib/libc.a
	$(CC) $(CFLAGS) -I $(DEBUGGER_DIR) -c -o $@ $^

debugger.elf: debugger.o libgdb.a libco.a libvspace.a libgdbcomp.o | $(LIONS_LIBC)/lib/libc.a
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

-include $(DEBUGGER_OFILES:.o=.d)
