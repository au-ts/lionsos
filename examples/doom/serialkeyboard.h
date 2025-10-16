/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * This file implements a state machine for parsing serial-encoded HID messages
 * and emulating a mouse and keyboard which react to the state.
 *
 * This file implements the state machine statically - i.e. it is
 * assumed that the user only will need one instance of this per
 * translation unit. If you need multiple keyboards/mice, you
 * can define SERIALKB_NON_STATIC and use alternative function
 * prototypes which accept a state entity.
 *
 * This is a library-isation of Julia's code in sddf/julia/hid-over-serial.
 *
 * TODO: add support for modifier keys
 */

#include <stdint.h>
#include <sddf/util/printf.h>
#include <os/sddf.h>
#include "usb_hid_keys.h"

/**
 * Simple parsing state machine.
 * in release/press waiting for next char.
 *
 *         /-  press (1) -> reset
 *         -  release (2) -> reset
 *       /
 * reset (0)
 *       \  - mouse button release (3) -> reset.
 *        \ - mouse button release (4) -> reset.
 *
 */

// Only needs 8 bit for key, we reserve high bits for metadata
typedef uint16_t hid_key_t;
#define HID_KEY_PRESSED_BIT (1 << 9)
#define HID_KEY_PRESS(x)    ((x & HID_KEY_PRESSED_BIT) != 0)
#define HID_KEYCODE(x)  (x & 0xff)

typedef enum hid_state {
    State_Reset = 0,
    State_KeyPress = 1,
    State_KeyRelease = 2,
    State_MouseKeyChange = 3,
    State_MouseMove = 4,
} hid_state_t;


typedef enum hid_cmd {
  Cmd_KeyPress = 0x0,
  Cmd_KeyRelease = 0x1,
  Cmd_MouseKeyChange = 0x2,
  Cmd_MouseMove = 0x3,
} hid_cmd_t;

enum MouseButton {
  LEFT_BUTTON   = 0x01,
  MIDDLE_BUTTON = 0x02,
  RIGHT_BUTTON  = 0x04
};

#define SERIAL_KB_NUM_KEYS (128)
typedef struct serialkb_state {
    uint8_t mouse_move_count;
    uint32_t dx;
    uint32_t dy;
    hid_state_t hidstate;
    uint8_t buttonState;
} serialkb_state_t;


#ifndef SERIAL_KB_NONSTATIC
static serialkb_state_t __state;
static serialkb_state_t *state = &__state;
#endif

/**
 * Protocol:
 *
 * Each byte is 8 bits, the high bits indicates the start for self-synchronising.
 *    (reader can wait til the next high bit)
 *
 * This gives us 7 bits to work with in each byte.
 *
 * First byte (with the high bit) is the command.
 *  0x0: key press, following byte is (oem) key.
 *  0x1: key release, following byte is (oem) key
 *  0x2: mouse key press change (following is mouse buttons)
 *  0x3: mouse move (following is 10 bytes of 2x32 (dx then dy) bits int in 7 bits)
 *
 */
#ifndef SERIAL_KB_NONSTATIC
static hid_key_t serialkb_input_serial_char(uint8_t c)
#else
hid_key_t serialkb_input_serial_char(serialkb_state_t *state, uint8_t c)
#endif
{
    switch (state->hidstate) {
    case State_Reset:
        /* high bit set == command */
        if (c & BIT(7)) {
            c &= ~BIT(7);
            switch ((hid_cmd_t)c) {
            case Cmd_KeyPress:
                state->hidstate = State_KeyPress;
                break;
            case Cmd_KeyRelease:
                state->hidstate = State_KeyRelease;
                break;
            case Cmd_MouseKeyChange:
                state->hidstate = State_MouseKeyChange;
                break;
            case Cmd_MouseMove:
                state->hidstate = State_MouseMove;
                state->mouse_move_count = 0;
                state->dx = 0;
                state->dy = 0;
                break;

            default:
                sddf_dprintf("serialkb: unknown command: '%d'", c);
                break;
            }
        } else {
            sddf_dprintf("serialkb: waiting for sync..., ignoring\n");
        }

        break;

    case State_KeyPress:
        if (c & BIT(7)) {
            state->hidstate = State_Reset;
            sddf_dprintf("serialkb: got command following command, protocol violation\n");
            break;
        }

        state->hidstate = State_Reset;
        sddf_dprintf("serialkb: pressed key '%d'\n", c);
        return (((hid_key_t) c) | HID_KEY_PRESSED_BIT);

    case State_KeyRelease:
        if (c & BIT(7)) {
            state->hidstate = State_Reset;
            sddf_dprintf("serialkb: got command following command, protocol violation\n");
            break;
        }

        state->hidstate = State_Reset;
        sddf_dprintf("serialkb: released key '%d'\n", c);
        // Pressed bit not set
        return ((hid_key_t) c);

    case State_MouseKeyChange:
        if (c & BIT(7)) {
            state->hidstate = State_Reset;
            sddf_dprintf("serialkb: got command following command, protocol violation\n");
            break;
        }

        state->hidstate = State_Reset;
        state->buttonState = c;
        sddf_dprintf("pressed mouse '%d':", c);
        if (state->buttonState & LEFT_BUTTON) {
            sddf_dprintf("serialkb:  left button");
        }
        if (state->buttonState & MIDDLE_BUTTON) {
            sddf_dprintf("serialkb:  middle button");
        }
        if (state->buttonState & RIGHT_BUTTON) {
            sddf_dprintf("serialkb:  right button");
        }
        sddf_dprintf("\n");
        break;

    case State_MouseMove:
        if (c & BIT(7)) {
            state->hidstate = State_Reset;
            sddf_dprintf("serialkb: got command following command, protocol violation\n");
            break;
        }

        uint32_t v = c;
        /* first five is dx (0-4) */
        if (state->mouse_move_count < 5) {
            uint8_t i = 7 * (state->mouse_move_count);
            state->dx |= (v << i);
        } else { /* rest (5-9) is dy */
            uint8_t i = 7 * (state->mouse_move_count - 5);
            state->dy |= (v << i);
        }

        state->mouse_move_count += 1;
        if (state->mouse_move_count == 10) {
            sddf_dprintf("serialkb: mouse move: dx=%d,dy=%d\n", state->dx, state->dy);
            state->hidstate = State_Reset;
        }
        break;

    default:
        assert(!"unknown state\n");
    }
    return HID_KEY_NONE;
}
