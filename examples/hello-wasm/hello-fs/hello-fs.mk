HELLO_WASM_HELLO_FS_DIR := $(abspath $(dir $(lastword ${MAKEFILE_LIST})))

app.wasm: ${HELLO_WASM_HELLO_FS_DIR}/main.c
	${WASI_SDK}/bin/clang -O3 \
        -z stack-size=4096 -Wl,--initial-memory=65536 \
        -o $@ $< \
        -Wl,--export=__data_end -Wl,--export=__heap_base \
        -Wl,--strip-all

app_wasm.h: app.wasm
	xxd -i $< > $@
