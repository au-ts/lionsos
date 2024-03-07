#include <stdint.h>

#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

#define MICROPY_ENABLE_COMPILER (1)
#define MICROPY_PY_BUILTINS_EVAL_EXEC (1)

// #define MICROPY_QSTR_EXTRA_POOL           mp_qstr_frozen_const_pool
#define MICROPY_MODULE_WEAK_LINKS (1)
#define MICROPY_ENABLE_GC (1)
#define MICROPY_HELPER_REPL (1)
#define MICROPY_PY_CMATH (1)
#define MICROPY_PY_SYS (1)
#define MICROPY_PY_FSTRINGS (1)
#define MICROPY_PY_BUILTINS_BYTEARRAY (1)
#define MICROPY_PY_BUILTINS_SLICE (1)
#define MICROPY_CPYTHON_COMPAT (1)
#define MICROPY_PY_CPRINGBUF (1)
#define MICROPY_PY_CPFS (1)
#define MICROPY_PY_UJSON (1)
#define MICROPY_PY_BUILTINS_SET (1)
#define MICROPY_PY_OS (1)
#define MICROPY_PY_IO (1)
#define MICROPY_VFS (1)
#define MICROPY_PY_MACHINE (1)
#define MICROPY_PY_MACHINE_I2C (1)
#define MICROPY_HW_ENABLE_HW_I2C (1)
#define MICROPY_READER_VFS (1)

#define MICROPY_ENABLE_EXTERNAL_IMPORT (1)

#define MICROPY_ALLOC_PATH_MAX (256)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT (16)
#define MICROPY_MODULE_OVERRIDE_MAIN_IMPORT (1)

// Configuration for the `time` module
#define MICROPY_PY_TIME                         (1)
#define MICROPY_PY_TIME_TIME_TIME_NS            (1)
// #define MICROPY_PY_TIME_INCLUDEFILE             "modtime.c"

// @ivanv: odd that 2KB did not work
// Allocate 1MB for the heap
#define MICROPY_HEAP_SIZE      (0x100000)

// Type definitions for the specific machine.

typedef intptr_t mp_int_t; // must be pointer size
typedef uintptr_t mp_uint_t; // must be pointer size
typedef long mp_off_t;

// We need to provide a declaration/definition of alloca().
#include <alloca.h>

// Define the port's name and hardware.
#define MICROPY_HW_BOARD_NAME "Odroid-C4"
#define MICROPY_HW_MCU_NAME   "Cortex A55"

#define MP_STATE_PORT MP_STATE_VM
