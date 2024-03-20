#include <microkit.h>
#include <string.h>
#include "py/runtime.h"
#include "micropython.h"
#include "../vmm/uio.h"

extern uintptr_t framebuffer_data_region;
/*
 * We get notified when we *can* write to the framebuffer, meaning that uPython
 * needs to wait until the framebuffer is ready.
 */
STATIC mp_obj_t fb_wait(void) {
    mp_blocking_events = mp_event_source_framebuffer;
    co_switch(t_event);
    /* Now we have received a notification from the VMM that the framebuffer is ready. */
    mp_blocking_events = mp_event_source_none;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_wait_obj, fb_wait);

STATIC mp_obj_t machine_fb_send(mp_obj_t buf_obj, mp_obj_t size_obj) {
    uint8_t *buf = MP_OBJ_TO_PTR(buf_obj);
    uint64_t size = mp_obj_get_int(size_obj);

    /* Need to now copy the data from MicroPython's framebuffer abstraction to
     * our shared memory region */

    memcpy((uint8_t *)framebuffer_data_region, buf, size);

    microkit_notify(FRAMEBUFFER_VMM_CH);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_fb_send_obj, machine_fb_send);

STATIC const mp_rom_map_elem_t fb_module_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_fb) },
    { MP_ROM_QSTR(MP_QSTR_wait), MP_ROM_PTR(&fb_wait_obj) },
    { MP_ROM_QSTR(MP_QSTR_machine_fb_send), MP_ROM_PTR(&machine_fb_send_obj) },
};
STATIC MP_DEFINE_CONST_DICT(fb_module_globals, fb_module_globals_table);

const mp_obj_module_t fb_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&fb_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_fb, fb_module);
