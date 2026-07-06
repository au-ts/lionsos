#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

DEBUGGER_NET_IFLAGS := -I$(DEBUGGER_DIR)/backends/net/include/lwip -I$(DEBUGGER_DIR)

debugger.o: $(DEBUGGER_DIR)/backends/net/debugger.c libsddf_util.a
	$(CC) -c $(CFLAGS) $^ -o $@ $(DEBUGGER_NET_IFLAGS)

tcp.o: $(DEBUGGER_DIR)/backends/net/tcp.c
	$(CC) -c $(CFLAGS) $^ -o $@ $(DEBUGGER_NET_IFLAGS)

export DEBUGGER_OBJS := debugger.o tcp.o
export DEBUGGER_LIBS := libsddf_util.a libvspace.a lib_sddf_lwip.a libgdb.a libco.a libvspace.a

export DEBUGGER_DEPS := $(DEBUGGER_OBJS:.o=.d)
# probably not ideal
export CFLAGS += -I$(DEBUGGER_DIR)/backends/net/include/lwip

debugger.elf: $(DEBUGGER_OBJS) $(DEBUGGER_LIBS)
	$(LD) $(LDFLAGS) $^ -o $@