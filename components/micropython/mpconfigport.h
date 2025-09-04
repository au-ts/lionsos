/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include <microkit.h>

#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

#define MICROPY_ENABLE_COMPILER (1)
#define MICROPY_PY_BUILTINS_EVAL_EXEC (1)

// #define MICROPY_QSTR_EXTRA_POOL           mp_qstr_frozen_const_pool
#define MICROPY_ENABLE_SOURCE_LINE (1)
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
#define MICROPY_PY_VFS (0)
#define MICROPY_READER_VFS (1)
#define MICROPY_PY_ASYNCIO (1)
#define MICROPY_PY_ASYNC_AWAIT (1)
#define MICROPY_PY_SELECT (1)
#define MICROPY_PY_LWIP (1)
#define MICROPY_ENABLE_SCHEDULER (1)
#define MICROPY_PY_ERRNO (1)
#define MICROPY_PY_BUILTINS_MEMORYVIEW (1)
#define MICROPY_PY_RE (1)
#define MICROPY_PY_DEFLATE (1)
#define MICROPY_PY_UCTYPES (1)
#define MICROPY_LONGINT_IMPL MICROPY_LONGINT_IMPL_MPZ
#define MICROPY_PY_JSON (1)
#define MICROPY_PY_IO_IOBASE (1)
#define MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (1)
#define MICROPY_EPOCH_IS_1970 (1)
#define MICROPY_PY_SYS_STDFILES (1)

#ifdef ENABLE_SERIAL_STDIO
#define MICROPY_PY_SYS_STDFILES (1)
#endif

/* For I2C connections */
#define MICROPY_PY_MACHINE (1)
#define MICROPY_PY_MACHINE_I2C (1)
#define MICROPY_HW_ENABLE_HW_I2C (1)

#ifdef ENABLE_FRAMEBUFFER
#define MICROPY_PY_FRAMEBUF (1)
#endif

#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_FLOAT)

#define MICROPY_ENABLE_EXTERNAL_IMPORT (1)

#define MICROPY_ALLOC_PATH_MAX (256)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT (16)
#define MICROPY_MODULE_OVERRIDE_MAIN_IMPORT (1)

// Configuration for the `time` module
#define MICROPY_PY_TIME                         (1)
#define MICROPY_PY_TIME_TIME_TIME_NS            (1)
// #define MICROPY_PY_TIME_INCLUDEFILE             "modtime.c"

// Allocate 16MB for the heap
#define MICROPY_HEAP_SIZE      (0x1000000)
#define MICROPY_STACK_SIZE      (0x100000)

// Type definitions for the specific machine.

typedef intptr_t mp_int_t; // must be pointer size
typedef uintptr_t mp_uint_t; // must be pointer size
typedef long mp_off_t;

// We need to provide a declaration/definition of alloca().
#include <alloca.h>

// Define the port's name and hardware.
#if defined(CONFIG_PLAT_ODROIDC4)
#define MICROPY_HW_BOARD_NAME "Odroid-C4"
#define MICROPY_HW_MCU_NAME   "Cortex A55"
#elif defined(CONFIG_PLAT_IMX8MM_EVK)
#define MICROPY_HW_BOARD_NAME "i.MX 8M Mini"
#define MICROPY_HW_MCU_NAME   "Cortex A53"
#elif defined(CONFIG_PLAT_IMX8MP_EVK)
#define MICROPY_HW_BOARD_NAME "i.MX 8MP"
#define MICROPY_HW_MCU_NAME   "Cortex A53"
#elif defined(CONFIG_PLAT_MAAXBOARD)
#define MICROPY_HW_BOARD_NAME "MaaXBoard"
#define MICROPY_HW_MCU_NAME   "Cortex A53"
#elif defined(CONFIG_PLAT_QEMU_ARM_VIRT)
#define MICROPY_HW_BOARD_NAME "QEMU virt AArch64"
#define MICROPY_HW_MCU_NAME   "Cortex A53"
#else
#error "Unknown platform given for MicroPython config"
#endif

#define MP_STATE_PORT MP_STATE_VM

void mp_hal_delay_us(mp_uint_t delay);
#define MICROPY_EVENT_POLL_HOOK do { mp_hal_delay_us(500); } while (0);

typedef uint32_t sys_prot_t;

#define MICROPY_PY_TIME_INCLUDEFILE "modtime_impl.h"
