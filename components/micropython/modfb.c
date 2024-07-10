#include <microkit.h>
#include <string.h>
#include <sddf/util/cache.h>
#include "py/runtime.h"
#include "micropython.h"
#include "../vmm/uio.h"

extern void *framebuffer_data_region;
/*
 * We get notified when we *can* write to the framebuffer, meaning that MicroPython
 * needs to wait until the framebuffer is ready.
 */
STATIC mp_obj_t fb_wait(void) {
    microkit_cothread_wait_on_channel(FRAMEBUFFER_VMM_CH);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_wait_obj, fb_wait);

STATIC mp_obj_t machine_fb_send(mp_obj_t buf_obj, mp_obj_t width_obj, mp_obj_t height_obj) {
    uint8_t *framebuffer;
    get_fb_base_addr(framebuffer_data_region, &framebuffer);

    fb_config_t *config = get_fb_config(framebuffer_data_region);

    size_t line_len = config->xres * (config->bpp/8);

    uint64_t width = mp_obj_get_int(width_obj);
    uint64_t height = mp_obj_get_int(height_obj);

    mp_buffer_info_t bufinfo;
    mp_get_buffer(buf_obj, &bufinfo, MP_BUFFER_READ);
    uint16_t *buf = (uint16_t *)bufinfo.buf;

    /* Need to now copy the data from MicroPython's framebuffer abstraction to
     * our shared memory region */

    /* Need to convert RGB565 to BGR888 */
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint16_t col = buf[x + (width * y)];
            uint8_t r5 = (col >> 11) & 0x1f;
            uint8_t g6 = (col >> 5) & 0x3f;
            uint8_t b5 = (col) & 0x1f;
            /* Conversion from 5-bit for red/blue and 6-bit for green to all being 8-bits. */
            uint8_t r8 = ( r5 * 527 + 23 ) >> 6;
            uint8_t g8 = ( g6 * 259 + 33 ) >> 6;
            uint8_t b8 = ( b5 * 527 + 23 ) >> 6;

            uint64_t location = (x * (config->bpp/8)) + (y * line_len);
            framebuffer[location] = b8;
            framebuffer[location + 1] = g8;
            framebuffer[location + 2] = r8;
            framebuffer[location + 3] = 0;
        }
    }

    /*
     * The UIO user-level program in the Linux virtual machine will have this framebuffer
     * data region mapped in as uncached as that is what the Linux UIO framework does.
     * This means that since we are using cached mappings within our client code, we must
     * propogate any cached writes so that it is visible by the Linux user-program that
     * talks to the real framebuffer.
     */
    cache_clean((uintptr_t)framebuffer_data_region, (uintptr_t)framebuffer_data_region + (width * height * 4));
    microkit_notify(FRAMEBUFFER_VMM_CH);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(machine_fb_send_obj, machine_fb_send);

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
