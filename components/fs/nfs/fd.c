/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include <assert.h>
#include <stdio.h>

#include "fd.h"

struct oftable_slot {
    enum {
        state_free,
        state_allocated,
        state_open_file,
        state_open_dir,
        state_busy_file,
        state_busy_dir
    } state;

    void *handle;
    uint64_t busy_count;
};

struct oftable_slot oftable[MAX_OPEN_FILES];

static int of_alloc(struct oftable_slot **slot) {
    for (uint64_t i = 0; i < MAX_OPEN_FILES; i++) {
        struct oftable_slot *candidate = &oftable[i];
        if (candidate->state == state_free) {
            candidate->state = state_allocated;
            *slot = candidate;
            return 0;
        }
    }
    return 1;
}

static int of_free(struct oftable_slot *of) {
    assert(of);
    switch (of->state) {
    case state_allocated:
        of->state = state_free;
        return 0;
    default:
        return -1;
    }
}

static int of_set_file(struct oftable_slot *of, struct nfsfh *file) {
    assert(of);
    switch (of->state) {
    case state_allocated:
        of->state = state_open_file;
        of->handle = file;
        return 0;
    default:
        return -1;
    }
}

static int of_set_dir(struct oftable_slot *of, struct nfsdir *dir) {
    assert(of);
    switch (of->state) {
    case state_allocated:
        of->state = state_open_dir;
        of->handle = dir;
        return 0;
    default:
        return -1;
    }
}

static int of_unset(struct oftable_slot *of) {
    assert(of);
    switch (of->state) {
    case state_open_dir:
    case state_open_file:
        of->state = state_allocated;
        of->handle = 0;
        return 0;
    default:
        return -1;
    }
}

static int of_begin_op_file(struct oftable_slot *of, struct nfsfh **file_handle_p) {
    assert(of);
    switch (of->state) {
    case state_open_file:
        of->state = state_busy_file;
        of->busy_count = 1;
        *file_handle_p = of->handle;
        return 0;
    case state_busy_file:
        of->busy_count++;
        *file_handle_p = of->handle;
        return 0;
    default:
        return -1;
    }
}

static int of_begin_op_dir(struct oftable_slot *of, struct nfsdir **dir_handle_p) {
    assert(of);
    switch (of->state) {
    case state_open_dir:
        of->state = state_busy_dir;
        of->busy_count = 1;
        *dir_handle_p = of->handle;
        return 0;
    case state_busy_dir:
        of->busy_count++;
        *dir_handle_p = of->handle;
        return 0;
    default:
        return -1;
    }
}

static int of_end_op(struct oftable_slot *of) {
    assert(of);
    switch (of->state) {
    case state_busy_dir:
        of->busy_count--;
        if (of->busy_count == 0) {
            of->state = state_open_dir;
        }
        return 0;
    case state_busy_file:
        of->busy_count--;
        if (of->busy_count == 0) {
            of->state = state_open_file;
        }
        return 0;
    default:
        return -1;
    }
}

static struct oftable_slot *fd_to_of(fd_t fd) {
    struct oftable_slot *slot = (struct oftable_slot *)fd;
    if (slot < oftable || slot >= oftable + MAX_OPEN_FILES) {
        return NULL;
    }
    return slot;
}

static fd_t of_to_fd(struct oftable_slot *of) {
    return (fd_t)of;
}

int fd_alloc(fd_t *fd) {
    struct oftable_slot *of = 0;
    int err = of_alloc(&of);
    if (!err) {
        *fd = of_to_fd(of);
    }
    return err;
}

int fd_free(fd_t fd) {
    struct oftable_slot *of = fd_to_of(fd);
    if (of == NULL) {
        return -1;
    }
    return of_free(of);
}

int fd_set_file(fd_t fd, struct nfsfh *file) {
    struct oftable_slot *of = fd_to_of(fd);
    if (of == NULL) {
        return -1;
    }
    return of_set_file(of, file);
}

int fd_set_dir(fd_t fd, struct nfsdir *dir) {
    struct oftable_slot *of = fd_to_of(fd);
    if (of == NULL) {
        return -1;
    }
    return of_set_dir(of, dir);
}

int fd_unset(fd_t fd) {
    struct oftable_slot *of = fd_to_of(fd);
    if (of == NULL) {
        return -1;
    }
    return of_unset(of);
}

int fd_begin_op_file(fd_t fd, struct nfsfh **file) {
    struct oftable_slot *of = fd_to_of(fd);
    if (of == NULL) {
        return -1;
    }
    return of_begin_op_file(of, file);
}

int fd_begin_op_dir(fd_t fd, struct nfsdir **dir) {
    struct oftable_slot *of = fd_to_of(fd);
    if (of == NULL) {
        return -1;
    }
    return of_begin_op_dir(of, dir);
}

void fd_end_op(fd_t fd) {
    struct oftable_slot *of = fd_to_of(fd);
    assert(of != NULL);
    int err = of_end_op(of);
    assert(err == 0);
}
