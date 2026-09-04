#pragma once
#include <stdarg.h>

static long sys_brk(va_list ap, microkit_child child);

static long sys_mmap(va_list ap, microkit_child child);

static long sys_munmap(va_list ap, microkit_child child);

static long sys_mprotect(va_list ap, microkit_child child);