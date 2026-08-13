#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
TAR ?= tar

BACKTRACER_DIR := $(LIONSOS)/components/backtracer
LLVM := llvm-project-22.1.8.src
LLVM_TAR := llvm-project-22.1.8.src.tar.xz
LLVM_URL := https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.8/llvm-project-22.1.8.src.tar.xz
LIBUNWIND := $(LLVM)/libunwind
LIBUNWIND_BUILD_DIR := ./libunwind

CFLAGS_backtracer := \
	$(CFLAGS)\
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(LIBUNWIND)/include \
	-I$(BOARD_DIR)/include \
	-I$(LIONS_LIBC)/include  -funwind-tables

LDFLAGS_backtracer := -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib
LIBS_backtracer := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a -lc

LLVM_CMAKE_FLAGS := \
		-DLLVM_ENABLE_RUNTIMES=libunwind\
		-DCMAKE_SYSTEM_NAME=Generic\
		-DCMAKE_C_COMPILER_TARGET=$(TARGET)\
		-DCMAKE_CXX_COMPILER_TARGET=$(TARGET)\
		-DCMAKE_ASM_COMPILER_TARGET=$(TARGET)\
		-DLIBUNWIND_IS_BAREMETAL=ON\
		-DLIBUNWIND_ENABLE_SHARED=OFF\
		-DLIBUNWIND_ENABLE_THREADS=OFF\
		-DLIBUNWIND_USE_COMPILER_RT=ON\
		-DLIBUNWIND_ENABLE_PEDANTIC=OFF\
		-DLIBUNWIND_ENABLE_ASSERTIONS=OFF\
		-DCMAKE_BUILD_TYPE=Debug\
		-DLIBUNWIND_ENABLE_STATIC=ON\
		-DCMAKE_C_COMPILER=$(CC)\
		-DCMAKE_CXX_COMPILER=$(CXX)\
		-DCMAKE_ASM_COMPILER=$(CC)\
		-DCMAKE_C_FLAGS="-I$(LIONS_LIBC)/include"\
		-DCMAKE_CXX_FLAGS="-I$(LIONS_LIBC)/include -fno-exceptions"\
		-DCMAKE_C_COMPILER_WORKS=ON\
		-DCMAKE_CXX_COMPILER_WORKS=ON\
		-DCMAKE_ASM_COMPILER_WORKS=ON

backtracer:
	mkdir -p $@

unwind_helpers.o: $(BACKTRACER_DIR)/unwind_helpers.c | $(LIONS_LIBC)/include backtracer $(LLVM)
	${CC} ${CFLAGS_backtracer} -c -o $@ $<

backtracer/backtracer.o: $(BACKTRACER_DIR)/backtracer.c | $(LIONS_LIBC)/include backtracer
	${CC} ${CFLAGS_backtracer} -c -o $@ $<

backtracer.elf: backtracer/backtracer.o libunwind.a | backtracer
	${LD} ${LDFLAGS_backtracer} -o $@ $^ ${LIBS_backtracer}

$(LLVM_TAR):
	wget $(LLVM_URL)

$(LLVM): $(LLVM_TAR)
	${TAR} xvf $< $@/{libunwind,runtimes,cmake,utils,third-party} $@/llvm/{cmake,utils}

$(LIBUNWIND_BUILD_DIR): | $(LLVM) $(LIONS_LIBC)/include
	cmake -B $(LIBUNWIND_BUILD_DIR) -S $(LLVM)/runtimes \
		$(LLVM_CMAKE_FLAGS)

libunwind.a: | $(LIONS_LIBC)/include $(LIBUNWIND_BUILD_DIR)
	${MAKE} -C $(LIBUNWIND_BUILD_DIR)
	cp $(LIBUNWIND_BUILD_DIR)/lib/libunwind.a $@

clean::
	${RM} -rf backtracer backtracer.elf unwind_helpers.o libunwind.a libunwind

-include unwind_helpers.d backtrace/backtracer.d
