#include <sel4cp.h>
#include "micropython.h"
#include "py/obj.h"
#include "drivers/clock/meson/timer.h"

mp_uint_t mp_hal_ticks_us(void) {
    sel4cp_dbg_puts("MICROPYTHON|ERROR: mp_hal_ticks_us is unimplemented\n");
    return 0;
}

mp_uint_t mp_hal_ticks_ms(void) {
    sel4cp_dbg_puts("MICROPYTHON|ERROR: ml_hal_ticks_ms is unimplemented\n");
    return 0;
}

mp_uint_t mp_hal_ticks_cpu(void) {
    sel4cp_dbg_puts("MICROPYTHON|ERROR: mp_hal_ticks_cpu is unimplemented\n");
    return 0;
}

uint64_t mp_hal_time_ns(void) {
    sel4cp_dbg_puts("MICROPYTHON|ERROR: mp_hal_time_ns is unimplemented\n");
    return 0;
}

void mp_hal_delay_us(mp_uint_t delay) {
    timer_set_timeout(delay * 1000);
    mp_blocking_events = mp_event_source_timer;
    co_switch(t_event);
    mp_blocking_events = mp_event_source_none;
}

void mp_hal_delay_ms(mp_uint_t delay) {
    mp_hal_delay_us(delay * 1000);
}

mp_obj_t mp_time_time_get(void) {
    return mp_obj_new_int(timer_time_now() / 1000 / 1000);
}
