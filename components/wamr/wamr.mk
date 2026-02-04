#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

WAMR_DIR := $(LIONSOS)/components/wamr
WAMR_ROOT := $(LIONSOS)/dep/wasm-micro-runtime

CFLAGS_wamr := \
	-I$(WAMR_DIR) \
	-I$(WAMR_DIR)/lwip_include \
	-I$(WAMR_DIR)/platform \
	-I$(WAMR_ROOT)/core/iwasm/include \
	-I$(WAMR_ROOT)/core/shared/platform/include \
	-I$(BUILD_DIR)

LIB_SDDF_LWIP_CFLAGS_wamr := ${CFLAGS_wamr}
LIBMICROKITCO_CFLAGS_wamr := ${CFLAGS_wamr}

tcp_wamr.o: $(LIONSOS)/lib/sock/tcp.c | $(LIONS_LIBC)/include
	${CC} ${CFLAGS} ${CFLAGS_wamr} -c -o $@ $<

WAMR_FILES := wamr.c
WAMR_OBJ := $(addprefix wamr/, $(WAMR_FILES:.c=.o))

CHECK_WAMR_FLAGS_MD5 := .wamr_cflags-$(shell echo -- $(CFLAGS) $(CFLAGS_wamr) | shasum | sed 's/ *-//')

$(CHECK_WAMR_FLAGS_MD5):
	-rm -f .wamr_cflags-*
	touch $@

$(WAMR_ROOT)/core/shared/platform/include:
	cd $(LIONSOS); git submodule update --init dep/wasm-micro-runtime

wamr/libvmlib.a: wamr | $(LIONS_LIBC)/include
	WAMR_LIBC=$(abspath $(LIONS_LIBC)) CC=$(CC) CPU=$(CPU) TARGET=$(TARGET) \
		cmake -S $(WAMR_DIR)/platform -B wamr \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE=$(WAMR_DIR)/platform/toolchain.cmake \
		-DSHARED_PLATFORM_CONFIG=$(WAMR_DIR)/platform/shared_platform.cmake \
		-DWAMR_BUILD_INTERP=1 \
		-DWAMR_BUILD_LIBC_BUILTIN=1 \
		-DWAMR_BUILD_LIBC_WASI=1 \
		-DWAMR_BUILD_REF_TYPES=1
	cmake --build wamr

define WAMR_RECIPE
$1.elf: $(WAMR_OBJ) lib_sddf_lwip_wamr.a wamr/libvmlib.a libmicrokitco_wamr.a tcp_wamr.o $1.o
	$(LD) $(LDFLAGS) -o $$@ $(LIBS) $$^

# Convert wasm binaries to ELFs for linking with the runtime.
# Note we rename the auto-generated symbols so that we can refer to them
# in the component code.
$1.o: $1.wasm
	llvm-objcopy --input-target binary --output-target elf64-$(ARCH) \
	--redefine-sym _binary_$1_wasm_start=_binary_app_wasm_start \
	--redefine-sym _binary_$1_wasm_end=_binary_app_wasm_end \
	--redefine-sym _binary_$1_wasm_size=_binary_app_wasm_size \
	$$^ $$@
endef

$(foreach t,$(WASM_TARGETS),$(eval $(call WAMR_RECIPE,$(t))))

wamr:
	mkdir -p $@

$(WAMR_OBJ): $(CHECK_WAMR_FLAGS_MD5)
$(WAMR_OBJ): $(WAMR_ROOT)/core/shared/platform/include
$(WAMR_OBJ): wamr
$(WAMR_OBJ): CFLAGS :=  $(CFLAGS) $(CFLAGS_wamr)

wamr/%.o: $(WAMR_DIR)/%.c | $(LIONS_LIBC)/include
	$(CC) -c $(CFLAGS) $< -o $@

-include $(WAMR_OBJ:.o=.d)
