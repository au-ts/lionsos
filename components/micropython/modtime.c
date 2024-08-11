/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <sddf/timer/client.h>
#include "micropython.h"
#include "py/obj.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "modtime_impl.h"

uint64_t mp_hal_time_ns(void) {
    return sddf_timer_time_now(TIMER_CH);
}

mp_uint_t mp_hal_ticks_us(void) {
    return mp_hal_time_ns() / 1000;
}

mp_uint_t mp_hal_ticks_ms(void) {
    return mp_hal_ticks_us() / 1000;
}

mp_uint_t mp_hal_ticks_cpu(void) {
    microkit_dbg_puts("MICROPYTHON|ERROR: mp_hal_ticks_cpu is unimplemented\n");
    return 0;
}

void mp_hal_delay_us(mp_uint_t delay) {
    sddf_timer_set_timeout(TIMER_CH, delay * 1000);
    microkit_cothread_wait_on_channel(TIMER_CH);
}

void mp_hal_delay_ms(mp_uint_t delay) {
    mp_hal_delay_us(delay * 1000);
}

mp_obj_t mp_time_time_get(void) {
    return mp_obj_new_int(sddf_timer_time_now(TIMER_CH) / 1000 / 1000);
}

mp_obj_t mp_time_localtime_get(void) {
    mp_raise_NotImplementedError(MP_ERROR_TEXT("real-time clock not available"));
    return mp_const_none;
}
