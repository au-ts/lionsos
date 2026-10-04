/*
 * Copyright 2026, LionsOS Contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * The protocol between the Files application and the PD that brokers the
 * filesystem for it.
 *
 * The FAT server can serve exactly one client: fs_server_config_t holds a
 * single client connection, so the Files application cannot hold a filesystem
 * connection of its own. It asks this broker instead, over one shared page
 * holding a single request and a single response, with channels used only as
 * wakeup hints.
 *
 * The broker owns the namespace. The application speaks the friendly names
 * ("C:\Applications"); the broker is the only component that turns one into a
 * real filesystem path and decides whether it may be served. That mapping and
 * the check behind it are policy in the broker, not an seL4 capability, and
 * the limits of that are recorded in examples/desktop/README.md.
 *
 * Requests are one at a time: the application sends one, then waits for the
 * reply carrying the same sequence number. Nothing here mutates anything.
 */

#pragma once

#include <stdint.h>

/* Channels; the two PDs number the same connection differently */
#define FILES_BROKER_CH 1   /* the broker, seen from the Files application */
#define FILES_SERVICE_CH 3  /* the Files application, seen from the broker */

#define FILES_PATH_MAX 128
#define FILES_NAME_MAX 48
#define FILES_ENTRIES_MAX 32
/* A preview, not a viewer: one request, bounded, and never a whole disk file */
#define FILES_READ_MAX 4096

/* What the application asks for */
#define FILES_REQ_LIST 1
#define FILES_REQ_READ 2

/* What came back */
#define FILES_OK 0
#define FILES_ERR_BAD_PATH 1  /* not a C:\ path, or one that escapes */
#define FILES_ERR_DENIED 2    /* outside the part of the namespace that is real */
#define FILES_ERR_NOT_FOUND 3 /* the path is real but nothing is there */
#define FILES_ERR_UNAVAILABLE 4 /* a part of the namespace not backed yet */
#define FILES_ERR_TOO_LARGE 5 /* asked for more than a response can carry */
#define FILES_ERR_IO 6        /* the filesystem itself failed */

typedef struct files_entry {
    char name[FILES_NAME_MAX];
    uint8_t is_dir;
    /* 0 for the parts of the namespace that have no backing store yet */
    uint8_t available;
    uint8_t reserved[2];
    uint64_t size;
} files_entry_t;

typedef struct files_request {
    uint32_t kind;
    uint32_t offset;
    uint32_t length;
    uint64_t seq;
    char path[FILES_PATH_MAX];
} files_request_t;

typedef struct files_response {
    uint64_t seq;
    uint32_t status;
    /* Entries listed, or bytes read */
    uint32_t count;
    union {
        files_entry_t entries[FILES_ENTRIES_MAX];
        uint8_t data[FILES_READ_MAX];
    };
} files_response_t;

typedef struct files_page {
    files_request_t request;
    files_response_t response;
} files_page_t;

/* Must match meta.py, the way GUI_SURFACE_REGION_SIZE does */
#define FILES_PAGE_REGION_SIZE 0x4000

_Static_assert(sizeof(files_page_t) <= FILES_PAGE_REGION_SIZE, "the shared page must fit in its region");
