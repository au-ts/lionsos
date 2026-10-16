/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <fat_config.h>
#include "ff.h"
#include <lions/fs/protocol.h>
#include <lions/fs/ramfs.h>

// For debug
#ifdef FAT_DEBUG_PRINT
#include <sddf/util/printf.h>
#define LOG_FATFS(...) do{ sddf_dprintf("FATFS|INFO: "); sddf_dprintf( __VA_ARGS__); } while(0)
#else
#define LOG_FATFS(...) do{}while(0)
#endif
