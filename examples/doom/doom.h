/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/time.h>
#include <libmicrokitco.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/config.h>
#include <sddf/timer/client.h>
#include <sddf/util/printf.h>
#include <lions/fs/config.h>
#include <lions/fs/protocol.h>

#include <hdmi/hdmi_data.h>
#include <hdmi/vic_table.h>
#include <api/frame_buffer.h>

#include "doomgeneric/doomgeneric/doomgeneric.h"
#include "doomgeneric/doomgeneric/doomkeys.h"
#include "usb_hid_keys.h"
/*#include "fs_blocking_calls.h"*/
/*#include "fs_client_helpers.h"*/


#define WORKER_STACK_SIZE 0x10000
#define DCSS_INIT_CH 42
#define DCSS_DRAW_CH 43

// Red-Green-Blue-Alpha pixel format, 1 byte per channel
#define FRAME_SZ_BYTES (1920 * 1080 * 4)

static inline uint8_t convertToDoomKey(unsigned int key)
{
    // We just use pure keyboard bindings and pretend the mouse is evil.
    switch (key)
    {
        case HID_KEY_ENTER:
            key = KEY_ENTER;
            break;
        case HID_KEY_ESC:
            key = KEY_ESCAPE;
            break;
        case HID_KEY_LEFT:
            key = KEY_LEFTARROW;
            break;
        case HID_KEY_RIGHT:
            key = KEY_RIGHTARROW;
            break;
        case HID_KEY_UP:
            key = KEY_UPARROW;
            break;
        case HID_KEY_DOWN:
            key = KEY_DOWNARROW;
            break;
        case HID_KEY_SPACE:
            key = KEY_FIRE;
            break;
        case HID_KEY_U:
            key = KEY_USE;
            break;
        // Hack: we don't support modifiers yet...
        case HID_KEY_SLASH:
            key = KEY_RSHIFT;
            break;
        default:
            key = tolower(key);
            break;
        }

    return key;
}
