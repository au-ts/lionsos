WAMR_DIR := $(LIONSOS)/components/wamr
WAMR_ROOT := $(LIONSOS)/dep/wasm-micro-runtime

CFLAGS_wamr := \
	-I$(LWIP)/include \
	-I$(WAMR_DIR) \
	-I$(WAMR_DIR)/lwip_include \
	-I$(WAMR_DIR)/platform \
	-I$(WAMR_ROOT)/core/iwasm/include \
	-I$(WAMR_ROOT)/core/shared/platform/include \
	-I$(BUILD_DIR) \
	-I$(LIBMICROKITCO_PATH)

LIB_SDDF_LWIP_CFLAGS_wamr := ${CFLAGS_wamr}
LIBMICROKITCO_CFLAGS_wamr := ${CFLAGS_wamr}

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

wamr.elf: $(WAMR_OBJ) lib_sddf_lwip_wamr.a wamr/libvmlib.a libmicrokitco_wamr.a
	$(LD) $(LDFLAGS) -o $@ $(LIBS) $^

wamr:
	mkdir -p $@

$(WAMR_OBJ): $(CHECK_WAMR_FLAGS_MD5)
$(WAMR_OBJ): $(WAMR_ROOT)/core/shared/platform/include
$(WAMR_OBJ): wamr
$(WAMR_OBJ): CFLAGS :=  $(CFLAGS) $(CFLAGS_wamr)

wamr/%.o: $(WAMR_DIR)/%.c app_wasm.h | $(LIONS_LIBC)/include
	$(CC) -c $(CFLAGS) $< -o $@

-include $(WAMR_OBJ:.o=.d)
