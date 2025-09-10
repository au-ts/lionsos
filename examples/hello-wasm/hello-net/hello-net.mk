WAMR_SOCKET := $(WAMR_ROOT)/core/iwasm/libraries/lib-socket
HELLO_WASM_HELLO_NET_DIR := $(abspath $(dir $(lastword ${MAKEFILE_LIST})))

app.wasm: ${HELLO_WASM_HELLO_NET_DIR}/main.c $(WAMR_SOCKET)/src/wasi/wasi_socket_ext.c
	${WASI_SDK}/bin/clang -O3 \
        -z stack-size=4096 -Wl,--initial-memory=65536 \
        -o $@ $^ \
        -Wl,--export=__data_end -Wl,--export=__heap_base \
        -Wl,--strip-all \
		-I$(WAMR_SOCKET)/inc

app_wasm.h: app.wasm
	xxd -i $< > $@
