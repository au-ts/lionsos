#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

debugger.o: $(DEBUGGER_DIR)/backends/net/debugger.c libsddf_util.a
	$(CC) -c $(CFLAGS) $(DEBUGGER_DIR)/backends/net/debugger.c -o $@ # There is something weird with $@ on the second build here???

tcp.o: $(DEBUGGER_DIR)/backends/net/tcp.c
	$(CC) -c $(CFLAGS) $(DEBUGGER_DIR)/backends/net/tcp.c -o $@

export DEBUGGER_OBJS := debugger.o tcp.o
export DEBUGGER_LIBS := libsddf_util.a libvspace.a lib_sddf_lwip.a libgdb.a libco.a libvspace.a

export DEBUGGER_DEPS := $(DEBUGGER_OBJS:.o=.d)

debugger.elf: $(DEBUGGER_OBJS) $(DEBUGGER_LIBS)
	$(LD) $(LDFLAGS) $^ -o $@