#
# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Include this snippet in your project Makefile to build
# the benchmark.elf and idle.elf programs.
#
BENCH_OBJS := benchmark/benchmark.o
IDLE_OBJS := benchmark/idle.o
LIBUTIL_DBG := libsddf_util_debug.a
LIBUTIL := libsddf_util.a

${BENCH_OBJS} ${IDLE_OBJS}: ${CHECK_FLAGS_BOARD_MD5} |benchmark
benchmark:
	mkdir -p benchmark

benchmark.elf: $(BENCH_OBJS) ${LIBUTIL}
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

idle.elf: $(IDLE_OBJS) ${LIBUTIL_DBG}
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@
