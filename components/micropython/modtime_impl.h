/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <sddf/timer/client.h>
#include "micropython.h"
#include "py/obj.h"
#include "py/mphal.h"

uint64_t mp_hal_ticks_ns(void);

mp_uint_t mp_hal_ticks_us(void);

mp_uint_t mp_hal_ticks_ms(void);

mp_uint_t mp_hal_ticks_cpu(void);

void mp_hal_delay_us(mp_uint_t delay);

void mp_hal_delay_ms(mp_uint_t delay);

mp_obj_t mp_time_time_get(void);

mp_obj_t mp_time_localtime_get(void);
