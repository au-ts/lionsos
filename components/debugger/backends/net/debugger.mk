#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
export DEBUGGER_OBJS := debugger.o tcp.o
export DEBUGGER_LIBS := libsddf_util.a libvspace.a lib_sddf_lwip.a libgdb.a libco.a libvspace.a

export DEBUGGER_DEPS := $(DEBUGGER_OBJS:.o=.d)

DEBUGGER_NET_IFLAGS := -I$(DEBUGGER_DIR)/backends/net/include/lwip -I$(DEBUGGER_DIR)
export debugger_CFLAGS += $(DEBUGGER_NET_IFLAGS)
# export CFLAGS +=

debugger.o: $(DEBUGGER_DIR)/backends/net/debugger.c
	@echo "cflags: $(debugger_CFLAGS)"
	@echo "carat: $^"
	@echo "at: $@"
	$(CC) $(debugger_CFLAGS) -c $^ -o $@

tcp.o: $(DEBUGGER_DIR)/backends/net/tcp.c
	$(CC) $(debugger_CFLAGS) -c $^ -o $@

debugger.elf: $(DEBUGGER_OBJS) $(DEBUGGER_LIBS)
	$(LD) $(debugger_LDFLAGS) $^ -o $@

# probably not ideal
export CFLAGS += -I$(DEBUGGER_DIR)/backends/net/include/lwip