/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/*
 * Translate an evdev key code to printable ASCII using a US layout.
 * Returns 0 for keys without a printable character.
 */
char keymap_to_ascii(uint16_t code, bool shift);
