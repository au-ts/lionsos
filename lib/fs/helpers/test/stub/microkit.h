/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Host stand-in for the Microkit runtime: the fs helpers only name the
 * channel type and notify.
 */
#pragma once

typedef unsigned int microkit_channel;

void microkit_notify(microkit_channel ch);
