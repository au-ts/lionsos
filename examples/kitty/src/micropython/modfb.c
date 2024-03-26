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

    printf("width: 0x%lx, height: 0x%lx, framebuffer addr: 0x%lx\n", width, height, framebuffer);

    /* Need to now copy the data from MicroPython's framebuffer abstraction to
     * our shared memory region */

    /* Need to convert RGB565 to BGR888 */
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint16_t col = buf[x + (width * y)];
            uint8_t r5 = (col >> 11) & 0x1f;
            uint8_t g6 = (col >> 5) & 0x3f;
            uint8_t b5 = (col) & 0x1f;
            // uint8_t r8 = r5 * 8;
            // uint8_t g8 = g6 * 4;
            // uint8_t b8 = b5 * 8;
            // printf("x: 0x%lx, y: 0x%lx, col: 0x%x r5: 0%x, g6: 0x%x, b5: 0x%x, r8: 0x%x, g8: 0x%x, b8: 0x%x\n", x, y, col, r5, g6, b5, r8, g8, b8);
            uint8_t r8 = ( r5 * 527 + 23 ) >> 6;
            uint8_t g8 = ( g6 * 259 + 33 ) >> 6;
            uint8_t b8 = ( b5 * 527 + 23 ) >> 6;
            // uint8_t alpha = 0;
            // uint32_t bgra = (b8 << 24) | (g8 << 16) | (r8 << 8) | alpha;

            uint64_t location = (x * (config->bpp/8)) + (y * line_len);
            framebuffer[location] = b8;
            framebuffer[location + 1] = g8;
            framebuffer[location + 2] = r8;
            framebuffer[location + 3] = 0;
            // printf("x: 0x%lx, y: 0x%lx, config->bpp: 0x%lx, line_len: 0x%lx\n", x, y, config->bpp, line_len);
            // printf("framebuffer: 0x%lx, location: 0x%lx, b8: 0x%lx, g8: 0x%lx, r8: 0x%lx, r5: 0x%lx, g6: 0x%lx, b5: 0x%lx\n", framebuffer, location, b8, g8, r8, r5, g6, b5);
            // framebuffer[x + (width * y)] = bgra;
        }
    }

    microkit_notify(FRAMEBUFFER_VMM_CH);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(machine_fb_send_obj, machine_fb_send);

STATIC mp_obj_t machine_fb_test(void) {
    uint8_t *fb_base;
    fb_config_t *config;
    config = get_fb_config(framebuffer_data_region);
    get_fb_base_addr(framebuffer_data_region, &fb_base);
    printf("xres: %d, yres: %d, bpp: %d\n", config->xres, config->yres, config->bpp);
    size_t line_len = config->xres * (config->bpp/8);
    int start = 100;
    int end = 300;
    for (int y = start; y < end; y++) {
        for (int x = start; x < end; x++) {

            uint64_t location = (x * (config->bpp/8)) + (y * line_len);
            // printf("UIO location: 0x%lx\n", location);

            // if (x == 100 && y == 100) {
            //     printf("RECT|INFO: first location: 0x%lx\n", location);
            // }
            // if (x == 299 && y == 299) {
            //     printf("RECT|INFO: final location: 0x%lx\n", location);
            // }

            *(fb_base + location) = 100;        // Some blue
            *(fb_base + location + 1) = 15+(x-100)/2;     // A little green
            *(fb_base + location + 2) = 200-(y-100)/5;    // A lot of red
            *(fb_base + location + 3) = 0;      // No transparency
        }
    }

    microkit_notify(FRAMEBUFFER_VMM_CH);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(machine_fb_test_obj, machine_fb_test);

STATIC const mp_rom_map_elem_t fb_module_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_fb) },
    { MP_ROM_QSTR(MP_QSTR_wait), MP_ROM_PTR(&fb_wait_obj) },
    { MP_ROM_QSTR(MP_QSTR_machine_fb_send), MP_ROM_PTR(&machine_fb_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_machine_fb_test), MP_ROM_PTR(&machine_fb_test_obj) },
};
STATIC MP_DEFINE_CONST_DICT(fb_module_globals, fb_module_globals_table);

const mp_obj_module_t fb_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&fb_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_fb, fb_module);
