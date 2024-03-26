#pragma once

#define UIO_INIT_ADDRESS 0x300000

/*
* Driver VM configured pixel format:
* Each pixel is 4 bytes, with the following format:
* BYTE 1: Blue
* BYTE 2: Green
* BYTE 3: Red
* BYTE 4: Alpha (transparency)
*/

typedef struct fb_config {
    uint32_t xres;
    uint32_t yres;
    uint32_t bpp;
} fb_config_t;

fb_config_t *get_fb_config(void *uio_map) {
    if (uio_map == NULL) {
        printf("UIO ERROR: uio_map is NULL\n");
        return NULL;
    }
    if ((uintptr_t)uio_map % sizeof(uint32_t) != 0) {
        printf("UIO ERROR: uio_map is not aligned with fb_config_t\n");
        return NULL;
    }
    return (fb_config_t *)uio_map;
}

void set_fb_config(void *uio_map, fb_config_t config) {
    if (uio_map == NULL) {
        printf("UIO ERROR: uio_map is NULL\n");
        return;
    }
    if ((uintptr_t)uio_map % sizeof(uint32_t) != 0) {
        printf("UIO ERROR: uio_map is not aligned with fb_config_t\n");
        return;
    }
    *(fb_config_t *)uio_map = config;
}

void get_fb_base_addr(void *uio_map, void **fb_base_addr) {
    if (uio_map == NULL) {
        printf("UIO ERROR: uio_map is NULL\n");
        *fb_base_addr = NULL;
        return;
    }
    if ((uintptr_t)uio_map % sizeof(uint32_t) != 0) {
        printf("UIO ERROR: uio_map is not aligned with fb_config_t\n");
        *fb_base_addr = NULL;
        return;
    }
    *fb_base_addr = (uint8_t *)uio_map + sizeof(fb_config_t);
}