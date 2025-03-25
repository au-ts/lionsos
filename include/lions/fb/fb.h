/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>

/*
 * This header is used in a couple of different contexts in order to facilitate the
 * use of a framebuffer setup via a Linux driver virtual machine.
 * This is done by a Linux user-space process via UIO and the VMM that is virtualising
 * that VM talking to a MicroPython PD. This allows a Python script to write to the
 * framebuffer, and is primarily used by our Kitty example.
 * This is not a principled way of doing this as it does not allow for sharing the device
 * or using a native driver.
 * In the future we will transition away from this, see https://github.com/au-ts/lionsos/issues/141.
 */

#define FB_UIO_INIT_ADDRESS 0x300000

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

fb_config_t *fb_config_get(void *uio_map) {
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

void fb_config_set(void *uio_map, fb_config_t config) {
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

void fb_base_addr(void *uio_map, uint8_t **addr) {
    if (uio_map == NULL) {
        printf("UIO ERROR: uio_map is NULL\n");
        *addr = NULL;
        return;
    }
    if ((uintptr_t)uio_map % sizeof(uint32_t) != 0) {
        printf("UIO ERROR: uio_map is not aligned with fb_config_t\n");
        *addr = NULL;
        return;
    }
    *addr = (uint8_t *)uio_map + sizeof(fb_config_t);
}
