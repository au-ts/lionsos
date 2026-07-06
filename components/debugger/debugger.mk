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

# This include adds the target for debugger.o
include $(DEBUGGER_DIR)/backends/$(DEBUGGER_BACKEND)/debugger.mk
DEBUGGER_INTEROP := libgdbcomp

$(DEBUGGER_INTEROP).o: $(DEBUGGER_DIR)/$(DEBUGGER_INTEROP).c | $(LIONS_LIBC)/lib/libc.a
	$(CC) $(CFLAGS) -I $(DEBUGGER_DIR) -c -o $@ $^

debugger.elf: $(DEBUGGER_OBJS) $(DEBUGGER_LIBS) | $(LIONS_LIBC)/lib/libc.a
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}

-include $(DEBUGGER_INTEROP_O:.o=.d) $(DEPS)
