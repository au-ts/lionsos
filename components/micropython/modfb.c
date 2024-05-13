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

    uint64_t width = mp_obj_get_int(width_obj);
    uint64_t height = mp_obj_get_int(height_obj);

    mp_buffer_info_t bufinfo;
    mp_get_buffer(buf_obj, &bufinfo, MP_BUFFER_READ);
    uint8_t *buf = (uint8_t *)bufinfo.buf;

    /* Need to now copy the data from MicroPython's framebuffer abstraction to
     * our shared memory region */

    // Scaling factor
    double scale_x = width / (double) config->xres;
    double scale_y = height / (double) config->yres;

    for (int dst_y = 0; dst_y < config->yres; dst_y++) {
        for (int dst_x = 0; dst_x < config->xres; dst_x++) {
            int src_x = dst_x * scale_x;
            int src_y = dst_y * scale_y;

            int src_offset = (src_y * width + src_x) * 2;
            int dst_offset = (dst_y * config->xres + dst_x) * 4;

            uint16_t src_val = *(uint16_t *)(buf + src_offset);

            uint8_t r5 = (src_val >> 11) & 0x1f;
            uint8_t g6 = (src_val >> 5) & 0x3f;
            uint8_t b5 = (src_val) & 0x1f;
            /* Conversion from 5-bit for red/blue and 6-bit for green to all being 8-bits. */
            uint8_t r8 = ( r5 * 527 + 23 ) >> 6;
            uint8_t g8 = ( g6 * 259 + 33 ) >> 6;
            uint8_t b8 = ( b5 * 527 + 23 ) >> 6;

            framebuffer[dst_offset + 0] = b8; // Blue
            framebuffer[dst_offset + 1] = g8; // Green
            framebuffer[dst_offset + 2] = r8; // Red
            framebuffer[dst_offset + 3] = 0; // Alpha
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
