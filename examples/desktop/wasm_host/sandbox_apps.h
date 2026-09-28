/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Apps running in sandboxes, each in a window of its own (SANDBOX=1, see sandbox.h) */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <microkit.h>

bool sandbox_apps_init(void);

/* Run an app in a free sandbox, granting what `caps_text` lists and the policy allows */
void sandbox_apps_launch(const char *name, const char *path, const char *caps_text, size_t caps_len);

/* How many copies of the app called `name` are running */
int sandbox_apps_count_running(const char *name);

/* Record a notification for sandbox_apps_process(); false if `ch` is not ours */
bool sandbox_apps_notified(microkit_channel ch);

/* Record a fault of sandbox `child` for sandbox_apps_process() */
void sandbox_apps_fault(microkit_child child, microkit_msginfo msginfo);

/* Handle what was recorded: input for apps' windows, requests from sandboxes, faults */
void sandbox_apps_process(void);

/* The timer fired: tick the apps that are due */
void sandbox_apps_tick(void);
