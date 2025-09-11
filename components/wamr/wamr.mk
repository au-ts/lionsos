WAMR_DIR := $(LIONSOS)/components/wamr
LWIP := $(SDDF)/network/ipstacks/lwip/src
WAMR_ROOT := $(LIONSOS)/dep/wasm-micro-runtime
WAMR := $(WAMR_ROOT)/core/shared/platform

CFLAGS_wamr := \
	-I$(MUSL)/include \
	-I$(WAMR_DIR)/lwip_include \
	-I$(LWIP)/include \
	-I$(LWIP)/include/ipv4

LIB_SDDF_LWIP_CFLAGS_wamr := ${CFLAGS_wamr}
include $(SDDF)/network/lib_sddf_lwip/lib_sddf_lwip.mk

WAMR_FILES := wamr.c
WAMR_OBJ := $(addprefix wamr/, $(WAMR_FILES:.c=.o))

CHECK_WAMR_FLAGS_MD5 := .wamr_cflags-$(shell echo -- $(CFLAGS) $(CFLAGS_wamr) | shasum | sed 's/ *-//')

$(CHECK_WAMR_FLAGS_MD5):
	-rm -f .wamr_cflags-*
	touch $@

# $(WAMR):
# 	cd $(LIONSOS); git submodule update --init dep/wasm-micro-runtime

# wamr/libvmlib.a: wamr
# 	MUSL=$(abspath $(MUSL)) CC=$(CC) CPU=$(CPU) TARGET=$(TARGET) \
# 		cmake -S $(WAMR_DIR)/platform -B wamr \
# 		-DCMAKE_BUILD_TYPE=Release \
# 		-DCMAKE_TOOLCHAIN_FILE=$(WAMR_DIR)/platform/toolchain.cmake \
# 		-DSHARED_PLATFORM_CONFIG=$(WAMR_DIR)/platform/shared_platform.cmake
# 	cmake --build wamr

LIB_POSIX_LIBC_INCLUDE := $(MUSL)/include
include $(LIONSOS)/lib/posix/lib_posix.mk

LIB_COMPILER_RT_LIBC_INCLUDE := $(MUSL)/include
include $(LIONSOS)/lib/compiler_rt/lib_compiler_rt.mk

wamr.elf: $(WAMR_OBJ) $(MUSL)/lib/libc.a lib_sddf_lwip_wamr.a lib_posix.a lib_compiler_rt.a
	$(LD) $(LDFLAGS) -o $@ $(LIBS) $^

wamr:
	mkdir -p $@

$(WAMR_OBJ): $(CHECK_WAMR_FLAGS_MD5)
$(WAMR_OBJ): $(MUSL)/lib/libc.a
# $(WAMR_OBJ): $(WAMR)/include
$(WAMR_OBJ): wamr
$(WAMR_OBJ): CFLAGS := $(CFLAGS_wamr) \
					  $(CFLAGS)

wamr/%.o: $(WAMR_DIR)/%.c
	$(CC) -c $(CFLAGS) $< -o $@

-include $(WAMR_OBJ:.o=.d)
