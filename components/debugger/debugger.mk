#
# Copyright 2024, UNSW (ABN 57 195 873 179)
#
# SPDX-License-Identifier: BSD-2-Clause
#
DEBUGGER_SUPPORTED_BACKENDS := \
	serial \
	net

ifndef DEBUGGER_BACKEND
  $(error Please specify DEBUGGER_BACKEND. Supported backends: ${DEBUGGER_SUPPORTED_BACKENDS})
endif

ifeq ($(findstring $(strip ${DEBUGGER_BACKEND}), ${DEBUGGER_SUPPORTED_BACKENDS}),)
  $(error ${DEBUGGER_BACKEND} is not a valid backend. Supported DEBUGGER_BACKEND: ${DEBUGGER_SUPPORTED_BACKENDS})
endif

ifndef NUM_DEBUGEES
  $(error Please specify NUM_DEBUGEES)
endif

export NUM_DEBUGEES := $(NUM_DEBUGEES)

export DEBUGGER_DIR := $(LIONSOS)/components/debugger
export debugger_CFLAGS := $(CFLAGS) -DNUM_DEBUGEES=$(NUM_DEBUGEES)
export debugger_LDFLAGS := $(LDFLAGS)

# This include adds the target for debugger.o
include $(DEBUGGER_DIR)/backends/$(DEBUGGER_BACKEND)/debugger.mk
# adds DEBUGGER_OBJS, DEBUGGER_DEPS


gdb_interop.o: $(DEBUGGER_DIR)/gdb_interop.c libgdb.a libvspace.a
	$(CC) $(debugger_CFLAGS) -I $(DEBUGGER_DIR) -c -o $@ $^

debugger.elf: $(DEBUGGER_OBJS) $(DEBUGGER_LIBS) gdb_interop.o libgdb.a libco.a
	${LD} ${debugger_LDFLAGS} -o $@ $^ ${LIBS}

-include $(DEBUGGER_INTEROP_O:.o=.d) $(DEBUGGER_DEPS)
