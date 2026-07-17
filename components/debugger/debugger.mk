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


export DEBUGGER_DIR := $(LIONSOS)/components/debugger

# This include adds the target for debugger.o
include $(DEBUGGER_DIR)/backends/$(DEBUGGER_BACKEND)/debugger.mk
# adds DEBUGGER_OBJS

gdb_interop.o: $(DEBUGGER_DIR)/gdb_interop.c libgdb.a libvspace.a
	$(CC) $(CFLAGS) -I $(DEBUGGER_DIR) -c -o $@ $^

debugger.elf: $(DEBUGGER_OBJS) $(DEBUGGER_LIBS) gdb_interop.o libgdb.a libco.a
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

-include $(DEBUGGER_INTEROP_O:.o=.d) $(DEPS)
