BACKTRACER_DIR := $(LIONSOS)/components/backtracer
LLVM := $(LIONSOS)/dep/llvm-project

CFLAGS_backtracer := \
	-target $(TARGET) \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(LIBUNWIND)/include \
	-I$(BOARD_DIR)/include \
	-I$(LIONS_LIBC)/include -O0 -ggdb -funwind-tables

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

unwind_helpers.o: $(BACKTRACER_DIR)/unwind_helpers.c | $(LIONS_LIBC)/include backtracer
	${CC} ${CFLAGS_backtracer} -c -o $@ $<

backtracer/backtracer.o: $(BACKTRACER_DIR)/backtracer.c | $(LIONS_LIBC)/include backtracer
	${CC} ${CFLAGS_backtracer} -c -o $@ $<

backtracer.elf: backtracer/backtracer.o libunwind.a | backtracer
	${LD} ${LDFLAGS_backtracer} -o $@ $^ ${LIBS_backtracer}

libunwind.a: | $(LIONS_LIBC)/include backtracer
	cmake -B $(BUILD_DIR)/libunwind -S $(LLVM)/runtimes \
		$(LLVM_CMAKE_FLAGS)

	cmake --build $(BUILD_DIR)/libunwind
	cp $(BUILD_DIR)/libunwind/lib/libunwind.a $@

clean::
	${RM} -rf backtracer backtracer.elf unwind_helpers.o libunwind.a libunwind
export PYTHONPATH := "$(BACKTRACER_DIR):$$PYTHONPATH:$(PYTHONPATH)"

# TODO: add dep files.
