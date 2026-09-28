/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Capabilities of WebAssembly apps.
 *
 * An app can do nothing by default. What it may do is listed in a text file
 * next to it on the disk, /apps/<name>.caps, one capability per line:
 *
 *   window                 draw in the host's window and receive its input
 *   timer                  read the time and get periodic ticks
 *   console                write lines to the serial console
 *   file /apps/<path>      read one file, which must lie under /apps
 *
 * Lines starting with '#' are comments. The host applies its own policy on
 * top: unknown or duplicate capabilities, files outside /apps and files that
 * do not exist are refused. Granted capabilities are installed in a table
 * private to the running app, and the app refers to them by handle, the
 * table index, which it looks up by the name used in the .caps file. Every
 * operation checks that the handle is live and of the right type. When the
 * app stops, every capability is revoked. Grants, refusals and revocations
 * are written to an audit log on the serial console.
 *
 * This is enforced by the host PD rather than by seL4, since apps run inside
 * it; the table plays the role a capability space plays for a PD. Built with
 * SANDBOX=1, apps run in a sandbox PD instead, and the host maps into it
 * only what the table grants (see sandbox.h).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CAPS_MAX 8
#define CAP_NAME_MAX 64

typedef enum cap_type {
    CAP_WINDOW = 1,
    CAP_TIMER,
    CAP_CONSOLE,
    CAP_FILE,
} cap_type_t;

typedef struct cap {
    bool live;
    cap_type_t type;
    /* As written in the .caps file, e.g. "file /apps/readme.txt" */
    char name[CAP_NAME_MAX];
    /* For CAP_FILE */
    char path[CAP_NAME_MAX];
    uint64_t size;
} cap_t;

typedef struct caps_table {
    cap_t caps[CAPS_MAX];
    int num_denied;
} caps_table_t;

/* Returns the size of the file at `path`, or false if it does not exist */
typedef bool (*caps_file_size_fn)(const char *path, uint64_t *size);

/* Prints one audit line for `app`; `fmt` is printf-style */
typedef void (*caps_audit_fn)(const char *app, const char *fmt, ...);

void caps_init(caps_file_size_fn file_size, caps_audit_fn audit);

/*
 * Parse the contents of a .caps file and apply the grant policy, filling
 * `table` (which is cleared first). With `quiet`, nothing is audited, which
 * is used to preview grants in the app list. Returns the number granted.
 */
int caps_grant(caps_table_t *table, const char *app, const char *text, size_t len, bool quiet);

/* Revoke every capability in `table`; returns how many were live */
int caps_revoke_all(caps_table_t *table, const char *app);

/* The handle of the live capability called `name`, or -1 */
int caps_lookup(caps_table_t *table, const char *name);

/*
 * The live capability behind `handle` if it has type `type`, or NULL. A
 * refusal is audited, naming the operation `op`, up to a per-run limit.
 */
cap_t *caps_check(caps_table_t *table, const char *app, int handle, cap_type_t type, const char *op);

/* Whether a live capability of `type` exists */
bool caps_have(caps_table_t *table, cap_type_t type);

/* Revoke one capability; returns 0, or -1 if the handle is not live */
int caps_drop(caps_table_t *table, const char *app, int handle);

/* A short description of what is granted, e.g. "window, timer, 1 file" */
void caps_summary(caps_table_t *table, char *buf, size_t len);
