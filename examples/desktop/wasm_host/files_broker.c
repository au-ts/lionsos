/*
 * Copyright 2026, LionsOS Contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * The filesystem broker behind the Files application.
 *
 * The FAT server serves one client, which is this PD, so a Files application
 * cannot hold a filesystem connection of its own. It sends requests here and
 * this is where a friendly name stops being a name and becomes a path.
 *
 * Two things therefore live here and nowhere else:
 *
 *   - the namespace: which C:\ names exist and what they mean;
 *   - the check: whether a resolved path may be served at all.
 *
 * The check is policy in this PD, enforced because nothing else can reach the
 * filesystem. It is not an seL4 capability and it does not confine a
 * compromised host; examples/desktop/README.md says so. What it does give is a
 * single place that decides, and a narrow one: only /apps is reachable, and
 * only for reading.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <microkit.h>
#include <sddf/util/printf.h>
#include <lions/fs/protocol.h>
#include <lions/fs/helpers.h>
#include "../../include/files_ns.h"
#include "wasm_host.h"

/* Patched in by the system description */
uintptr_t files_page_vaddr;

static files_page_t *page;

/* The one part of the namespace with something behind it */
#define APPS_DIR "/apps"

/*
 * The rest of the namespace, shown so the shape is visible and honest rather
 * than silently absent.
 */
static const char *const unbacked_roots[] = { "Users", "System", "Shared", "Devices" };
#define NUM_UNBACKED_ROOTS 4

void files_broker_init(void)
{
    page = (files_page_t *)files_page_vaddr;
    memset(&page->response, 0, sizeof(page->response));
}

static void reply(uint64_t seq, uint32_t status, uint32_t count)
{
    page->response.seq = seq;
    page->response.status = status;
    page->response.count = count;
    microkit_notify(FILES_SERVICE_CH);
}

static bool eq_ignore_case(const char *a, const char *b)
{
    for (size_t i = 0;; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca += 'a' - 'A';
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb += 'a' - 'A';
        }
        if (ca != cb) {
            return false;
        }
        if (ca == '\0') {
            return true;
        }
    }
}

/*
 * Turn a friendly path into a filesystem path, or say why not.
 *
 * Accepts "C:\..." and "C:/...". Returns FILES_OK with `out` filled for
 * anything under Applications, FILES_ERR_UNAVAILABLE for the parts of the
 * namespace that have no backing store yet, and FILES_ERR_BAD_PATH or
 * FILES_ERR_DENIED for anything that is not a path we should serve.
 */
static uint32_t resolve(const char *display, char *out, size_t out_len, bool *is_root)
{
    *is_root = false;

    if (strlen(display) >= FILES_PATH_MAX) {
        return FILES_ERR_BAD_PATH;
    }
    /* The volume name is presentation only */
    if (!(display[0] == 'C' || display[0] == 'c') || display[1] != ':') {
        return FILES_ERR_BAD_PATH;
    }
    const char *rest = display + 2;
    if (*rest != '\\' && *rest != '/') {
        return FILES_ERR_BAD_PATH;
    }
    rest++;

    /* C:\ */
    if (*rest == '\0') {
        *is_root = true;
        return FILES_OK;
    }

    /*
     * Walk the components rather than trusting a concatenation, so that ".."
     * and a stray separator cannot reach outside. Applications is the only
     * root that maps to storage.
     */
    char component[FILES_NAME_MAX];
    size_t len = 0;
    const char *p = rest;
    bool applications = false;

    for (;;) {
        char c = *p;
        if (c == '\0' || c == '\\' || c == '/') {
            if (len == 0) {
                return FILES_ERR_BAD_PATH;
            }
            component[len] = '\0';
            if (strcmp(component, ".") == 0 || strcmp(component, "..") == 0) {
                return FILES_ERR_BAD_PATH;
            }
            if (!applications) {
                if (eq_ignore_case(component, "Applications")) {
                    applications = true;
                    if (out_len < strlen(APPS_DIR) + 1) {
                        return FILES_ERR_BAD_PATH;
                    }
                    strcpy(out, APPS_DIR);
                } else {
                    bool known = false;
                    for (size_t i = 0; i < NUM_UNBACKED_ROOTS; i++) {
                        if (eq_ignore_case(component, unbacked_roots[i])) {
                            known = true;
                        }
                    }
                    return known ? FILES_ERR_UNAVAILABLE : FILES_ERR_DENIED;
                }
            } else {
                size_t used = strlen(out);
                if (used + 1 + len >= out_len) {
                    return FILES_ERR_BAD_PATH;
                }
                out[used] = '/';
                memcpy(out + used + 1, component, len + 1);
            }
            len = 0;
            if (c == '\0') {
                break;
            }
            p++;
            continue;
        }
        if (len + 1 >= sizeof(component)) {
            return FILES_ERR_BAD_PATH;
        }
        component[len++] = c;
        p++;
    }

    /* Belt and braces: whatever came out must still be under /apps */
    if (!applications || strncmp(out, APPS_DIR, strlen(APPS_DIR)) != 0) {
        return FILES_ERR_DENIED;
    }
    return FILES_OK;
}

static void fill_root(void)
{
    files_entry_t *e = page->response.entries;
    uint32_t n = 0;

    strcpy(e[n].name, "Applications");
    e[n].is_dir = 1;
    e[n].available = 1;
    e[n].size = 0;
    n++;

    for (size_t i = 0; i < NUM_UNBACKED_ROOTS && n < FILES_ENTRIES_MAX; i++) {
        strcpy(e[n].name, unbacked_roots[i]);
        e[n].is_dir = 1;
        /* Shown, but nothing is behind it yet */
        e[n].available = 0;
        e[n].size = 0;
        n++;
    }

    reply(page->request.seq, FILES_OK, n);
}

/* List a directory of the one backed root */
static void list_dir(const char *path)
{
    fs_cmpl_t cmpl;
    if (!fs_path_command(FS_CMD_DIR_OPEN, path, 0, &cmpl)) {
        reply(page->request.seq, FILES_ERR_NOT_FOUND, 0);
        return;
    }
    uint64_t dir = cmpl.data.dir_open.fd;

    uint32_t n = 0;
    ptrdiff_t buf;
    if (fs_buffer_allocate(&buf) == 0) {
        while (n < FILES_ENTRIES_MAX) {
            int err = fs_command_blocking(&cmpl, (fs_cmd_t) {
                .type = FS_CMD_DIR_READ,
                .params.dir_read = { .fd = dir, .buf = { .offset = buf, .size = FS_BUFFER_SIZE } },
            });
            if (err || cmpl.status != FS_STATUS_SUCCESS) {
                break;
            }
            size_t name_len = MIN(cmpl.data.dir_read.path_len, (uint64_t)FS_BUFFER_SIZE);
            const char *name = fs_buffer_ptr(buf);
            if (name_len == 0 || name_len >= FILES_NAME_MAX) {
                break;
            }
            /* FatFS reports these; they are not things in the folder */
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                continue;
            }

            files_entry_t *e = &page->response.entries[n];
            memcpy(e->name, name, name_len);
            e->name[name_len] = '\0';
            e->is_dir = 0;
            e->available = 1;
            e->size = 0;

            /* The size costs an open, but the alternative is showing nothing */
            char child[FILES_PATH_MAX];
            if (strlen(path) + 1 + name_len < sizeof(child)) {
                snprintf(child, sizeof(child), "%s/%s", path, e->name);
                uint64_t size;
                if (file_size(child, &size)) {
                    e->size = size;
                }
            }
            n++;
        }
        fs_buffer_free(buf);
    }
    fs_command_blocking(&cmpl, (fs_cmd_t) { .type = FS_CMD_DIR_CLOSE, .params.dir_close.fd = dir });

    reply(page->request.seq, n > 0 || cmpl.status == FS_STATUS_SUCCESS ? FILES_OK : FILES_ERR_NOT_FOUND, n);
}

static void read_preview(const char *path, uint32_t offset, uint32_t length)
{
    if (length > FILES_READ_MAX) {
        reply(page->request.seq, FILES_ERR_TOO_LARGE, 0);
        return;
    }

    int64_t n = read_file(path, offset, page->response.data, length);
    if (n < 0) {
        reply(page->request.seq, FILES_ERR_NOT_FOUND, 0);
        return;
    }
    reply(page->request.seq, FILES_OK, (uint32_t)n);
}

/*
 * Called from the host's cothread, so the filesystem calls below may block.
 */
void files_broker_handle(void)
{
    files_request_t req = page->request;
    char path[FILES_PATH_MAX];
    bool is_root;

    uint32_t status = resolve(req.path, path, sizeof(path), &is_root);
    if (status != FILES_OK) {
        /* The root is synthetic, so resolve reports it before this branch */
        if (is_root && req.kind == FILES_REQ_LIST) {
            fill_root();
            return;
        }
        reply(req.seq, status, 0);
        return;
    }

    if (is_root) {
        if (req.kind == FILES_REQ_LIST) {
            fill_root();
        } else {
            reply(req.seq, FILES_ERR_BAD_PATH, 0);
        }
        return;
    }

    if (req.kind == FILES_REQ_LIST) {
        list_dir(path);
    } else if (req.kind == FILES_REQ_READ) {
        read_preview(path, req.offset, req.length);
    } else {
        reply(req.seq, FILES_ERR_BAD_PATH, 0);
    }
}
