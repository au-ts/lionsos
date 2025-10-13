#
# Copyright 2025, UNSW (ABN 57 195 873 179)
#
# SPDX-License-Identifier: BSD-2-Clause
#

# @kwinter: Clean this up, this is taken straight from the libgdb examples

LIBS := --start-group -lmicrokit -Tmicrokit.ld -lc libsddf_util_debug.a libvspace.a --end-group

debugger.o: $(TOP)/apps/debugger/debugger.c | $(SDDF_LIBC_INCLUDE)
    $(CC) -c $(CFLAGS) $(TOP)/apps/debugger/debugger.c -o $@ # There is something weird with $@ on the second build here???

DEBUGGER_OBJS := debugger.o

export DEPS := $(DEBUGGER_OBJS:.o=.d)

${DEBUGGER_OBJS}: ${CHECK_FLAGS_BOARD_MD5}
debugger.elf: $(DEBUGGER_OBJS) libsddf_util.a libvspace.a libgdb.a libco.a
    $(LD) $(LDFLAGS) $(DEBUGGER_OBJS) libsddf_util.a libvspace.a libgdb.a libco.a $(LIBS) -o $@
