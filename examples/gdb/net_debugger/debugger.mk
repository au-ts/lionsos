#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

debugger.o: $(TOP)/net_debugger/debugger.c libsddf_util.a
	$(CC) -c $(CFLAGS) $(TOP)/net_debugger/debugger.c -o $@ # There is something weird with $@ on the second build here???

tcp.o: $(TOP)/net_debugger/tcp.c
	$(CC) -c $(CFLAGS) $(TOP)/net_debugger/tcp.c -o $@

DEBUGGER_OBJS := debugger.o tcp.o

export DEPS := $(DEBUGGER_OBJS:.o=.d)

${DEBUGGER_OBJS}: ${CHECKfFLAGS_BOARD_MD5}
debugger.elf: $(DEBUGGER_OBJS) libsddf_util.a libvspace.a lib_sddf_lwip.a libgdb.a libco.a libvspace.a
	$(LD) $(LDFLAGS) $(DEBUGGER_OBJS) libsddf_util.a lib_sddf_lwip.a libgdb.a libvspace.a libco.a $(LIBS) -o $@