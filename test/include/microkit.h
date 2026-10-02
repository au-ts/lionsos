/*
 * Copyright 2026, LionsOS Contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

/*
 * A stand-in for the Microkit SDK's <microkit.h>, so that the pure-logic parts of
 * LionsOS can be compiled and run on the host. Only the declarations that
 * lib/fs/helpers/helpers.c actually needs are provided; anything else is a
 * deliberate compile error, so this stub cannot silently drift from the real
 * header.
 *
 * This header is reachable ONLY via the include path set up in test/Makefile.
 * No target build puts test/include on its include path.
 */

#include <stdint.h>

typedef uint64_t microkit_channel;

void microkit_notify(microkit_channel ch);