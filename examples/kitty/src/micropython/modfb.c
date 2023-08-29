#include <sel4cp.h>
#include "py/runtime.h"
#include "micropython.h"
#include "../vmm/uio.h"

uint8_t *uio_framebuffer_region;

/*
 * We get notified when we *can* write to the framebuffer, meaning that uPython
 * needs to wait until the framebuffer is ready.
 */

STATIC mp_obj_t fb_set_pixel(mp_obj_t x_o, mp_obj_t y_o, mp_obj_t rgba_o) {
    int x = mp_obj_get_int(x_o);
    int y = mp_obj_get_int(y_o);
    int rgba = mp_obj_get_int(rgba_o);

    int r = rgba & 0xff000000;
    int g = rgba & 0x00ff0000;
    int b = rgba & 0x0000ff00;
    int a = rgba & 0x000000ff;

    size_t offset = (x * (BPP/8)) + (y * LINE_LEN);

    *(uio_framebuffer_region + offset) = b;
    *(uio_framebuffer_region + offset + 1) = g;
    *(uio_framebuffer_region + offset + 2) = r;
    *(uio_framebuffer_region + offset + 3) = a;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(fb_set_pixel_obj, fb_set_pixel);

STATIC mp_obj_t fb_wait(void) {
    mp_blocking_events = mp_event_source_framebuffer;
    co_switch(t_event);
    /* Now we have received a notification from the VMM that the framebuffer is ready. */
    mp_blocking_events = mp_event_source_none;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_wait_obj, fb_wait);

STATIC mp_obj_t fb_flush(void) {
    sel4cp_notify(VMM_CH);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_flush_obj, fb_flush);

STATIC const mp_rom_map_elem_t fb_module_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_fb) },
    { MP_ROM_QSTR(MP_QSTR_set_pixel), MP_ROM_PTR(&fb_set_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_wait), MP_ROM_PTR(&fb_wait_obj) },
    { MP_ROM_QSTR(MP_QSTR_flush), MP_ROM_PTR(&fb_flush_obj) },
};
STATIC MP_DEFINE_CONST_DICT(fb_module_globals, fb_module_globals_table);

const mp_obj_module_t fb_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&fb_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_fb, fb_module);
