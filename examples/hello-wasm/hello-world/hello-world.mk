HELLO_WASM_HELLO_WORLD_DIR := $(abspath $(dir $(lastword ${MAKEFILE_LIST})))

app.wasm: ${HELLO_WASM_HELLO_WORLD_DIR}/main.c
	${WASI_SDK}/bin/clang -O3 \
        -z stack-size=4096 -Wl,--initial-memory=65536 \
        -o $@ $< \
        -Wl,--export=main -Wl,--export=__main_argc_argv \
        -Wl,--export=__data_end -Wl,--export=__heap_base \
        -Wl,--strip-all,--no-entry \
        -Wl,--allow-undefined \
        -nostdlib

app_wasm.h: app.wasm
	xxd -i $< > $@
