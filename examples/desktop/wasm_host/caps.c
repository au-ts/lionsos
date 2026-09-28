/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "caps.h"

#define APPS_PREFIX "/apps/"
/* Refusals audited per run, so that a misbehaving app cannot flood the console */
#define MAX_AUDITED_REFUSALS 16

static caps_file_size_fn file_size_fn;
static caps_audit_fn audit_fn;
static int refusals_audited;

void caps_init(caps_file_size_fn file_size, caps_audit_fn audit)
{
    file_size_fn = file_size;
    audit_fn = audit;
}

static const char *type_name(cap_type_t type)
{
    switch (type) {
    case CAP_WINDOW:
        return "window";
    case CAP_TIMER:
        return "timer";
    case CAP_CONSOLE:
        return "console";
    case CAP_FILE:
        return "file";
    }
    return "?";
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/* Whether a file path may be granted; sets *reason if not */
static bool file_allowed(const char *path, uint64_t *size, const char **reason)
{
    if (strncmp(path, APPS_PREFIX, strlen(APPS_PREFIX)) != 0 || strlen(path) == strlen(APPS_PREFIX)) {
        *reason = "files must lie under " APPS_PREFIX;
        return false;
    }
    for (const char *p = path; *p != '\0'; p++) {
        if (p[0] == '.' && p[1] == '.') {
            *reason = "'..' is not allowed in paths";
            return false;
        }
    }
    if (!file_size_fn || !file_size_fn(path, size)) {
        *reason = "no such file";
        return false;
    }
    return true;
}

/* Apply the grant policy to one line; returns the reason for a refusal, or NULL */
static const char *grant_line(caps_table_t *table, const char *line)
{
    cap_t cap = { .live = true };
    if (strlen(line) >= CAP_NAME_MAX) {
        return "line too long";
    }
    strcpy(cap.name, line);

    if (strcmp(line, "window") == 0) {
        cap.type = CAP_WINDOW;
    } else if (strcmp(line, "timer") == 0) {
        cap.type = CAP_TIMER;
    } else if (strcmp(line, "console") == 0) {
        cap.type = CAP_CONSOLE;
    } else if (strncmp(line, "file", 4) == 0 && is_space(line[4])) {
        const char *path = line + 5;
        while (is_space(*path)) {
            path++;
        }
        const char *reason;
        if (!file_allowed(path, &cap.size, &reason)) {
            return reason;
        }
        cap.type = CAP_FILE;
        strcpy(cap.path, path);
    } else {
        return "unknown capability";
    }

    int free_slot = -1;
    for (int i = 0; i < CAPS_MAX; i++) {
        if (!table->caps[i].live) {
            if (free_slot < 0) {
                free_slot = i;
            }
            continue;
        }
        if (strcmp(table->caps[i].name, cap.name) == 0) {
            return "duplicate";
        }
        if (cap.type != CAP_FILE && table->caps[i].type == cap.type) {
            return "only one allowed";
        }
    }
    if (free_slot < 0) {
        return "too many capabilities";
    }
    table->caps[free_slot] = cap;
    return NULL;
}

int caps_grant(caps_table_t *table, const char *app, const char *text, size_t len, bool quiet)
{
    memset(table, 0, sizeof(*table));
    refusals_audited = 0;

    int granted = 0;
    size_t pos = 0;
    while (pos < len) {
        /* Take one line, trimmed */
        size_t end = pos;
        while (end < len && text[end] != '\n') {
            end++;
        }
        size_t start = pos;
        size_t stop = end;
        pos = end + 1;
        while (start < stop && is_space(text[start])) {
            start++;
        }
        while (stop > start && is_space(text[stop - 1])) {
            stop--;
        }
        if (start == stop || text[start] == '#') {
            continue;
        }

        char line[CAP_NAME_MAX + 1];
        size_t n = stop - start;
        if (n > CAP_NAME_MAX) {
            n = CAP_NAME_MAX;
        }
        memcpy(line, text + start, n);
        line[n] = '\0';

        const char *reason = grant_line(table, line);
        if (reason) {
            table->num_denied++;
            if (!quiet && audit_fn) {
                audit_fn(app, "refused '%s': %s", line, reason);
            }
            continue;
        }
        granted++;
        if (!quiet && audit_fn) {
            int handle = caps_lookup(table, line);
            audit_fn(app, "granted #%d %s", handle, line);
        }
    }
    return granted;
}

int caps_revoke_all(caps_table_t *table, const char *app)
{
    int revoked = 0;
    for (int i = 0; i < CAPS_MAX; i++) {
        if (table->caps[i].live) {
            table->caps[i].live = false;
            revoked++;
        }
    }
    if (audit_fn && revoked) {
        audit_fn(app, "revoked all %d capabilities", revoked);
    }
    return revoked;
}

int caps_lookup(caps_table_t *table, const char *name)
{
    for (int i = 0; i < CAPS_MAX; i++) {
        if (table->caps[i].live && strcmp(table->caps[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

cap_t *caps_check(caps_table_t *table, const char *app, int handle, cap_type_t type, const char *op)
{
    const char *reason = NULL;
    if (handle < 0 || handle >= CAPS_MAX || !table->caps[handle].live) {
        reason = "no such capability";
    } else if (table->caps[handle].type != type) {
        reason = "wrong type of capability";
    } else {
        return &table->caps[handle];
    }

    if (audit_fn && refusals_audited < MAX_AUDITED_REFUSALS) {
        refusals_audited++;
        audit_fn(app, "denied %s on handle %d (%s)%s", op, handle, reason,
                 refusals_audited == MAX_AUDITED_REFUSALS ? ", not auditing further refusals" : "");
    }
    return NULL;
}

bool caps_have(caps_table_t *table, cap_type_t type)
{
    for (int i = 0; i < CAPS_MAX; i++) {
        if (table->caps[i].live && table->caps[i].type == type) {
            return true;
        }
    }
    return false;
}

int caps_drop(caps_table_t *table, const char *app, int handle)
{
    if (handle < 0 || handle >= CAPS_MAX || !table->caps[handle].live) {
        return -1;
    }
    table->caps[handle].live = false;
    if (audit_fn) {
        audit_fn(app, "dropped #%d %s", handle, table->caps[handle].name);
    }
    return 0;
}

void caps_summary(caps_table_t *table, char *buf, size_t len)
{
    int files = 0;
    size_t used = 0;
    buf[0] = '\0';
    for (cap_type_t type = CAP_WINDOW; type <= CAP_CONSOLE; type++) {
        if (caps_have(table, type)) {
            used += snprintf(buf + used, used < len ? len - used : 0, "%s%s", used ? ", " : "", type_name(type));
        }
    }
    for (int i = 0; i < CAPS_MAX; i++) {
        if (table->caps[i].live && table->caps[i].type == CAP_FILE) {
            files++;
        }
    }
    if (files) {
        snprintf(buf + used, used < len ? len - used : 0, "%s%d file%s", used ? ", " : "", files,
                 files == 1 ? "" : "s");
    } else if (used == 0) {
        snprintf(buf, len, "nothing");
    }
}
