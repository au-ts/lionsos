#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

export DEBUGGER_OBJS := debugger.o
export DEBUGGER_DEPS := $(DEBUGGER_OBJS:.o=.d)
export DEBUGGER_LIBS := 

debugger.o: $(DEBUGGER_DIR)/backends/serial/debugger.c
	$(CC) $(debugger_CFLAGS) -I $(DEBUGGER_DIR) -c -o $@ $^
