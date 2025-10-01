/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include <assert.h>
#include <stddef.h>

#include <lions/fs/server.h>

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
    uint64_t generation;
};

struct oftable_slot oftable[MAX_OPEN_FILES] = {
    (struct oftable_slot){
        .state = state_allocated,
    },
    (struct oftable_slot){
        .state = state_allocated,
    },
    (struct oftable_slot){
        .state = state_allocated,
    },
};

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
        of->generation++;
        return 0;
    default:
        return -1;
    }
}

static int of_set_file(struct oftable_slot *of, void *file_handle) {
    assert(of);
    switch (of->state) {
    case state_allocated:
        of->state = state_open_file;
        of->handle = file_handle;
        return 0;
    default:
        return -1;
    }
}

static int of_set_dir(struct oftable_slot *of, void *dir_handle) {
    assert(of);
    switch (of->state) {
    case state_allocated:
        of->state = state_open_dir;
        of->handle = dir_handle;
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

static int of_begin_op(struct oftable_slot *of, void **handle_p) {
    assert(of);
    switch (of->state) {
    case state_open_file:
        of->state = state_busy_file;
        of->busy_count = 1;
        *handle_p = of->handle;
        return 0;
    case state_busy_file:
        of->busy_count++;
        *handle_p = of->handle;
        return 0;
    case state_open_dir:
        of->state = state_busy_dir;
        of->busy_count = 1;
        *handle_p = of->handle;
        return 0;
    case state_busy_dir:
        of->busy_count++;
        *handle_p = of->handle;
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
    uint64_t index = fd % MAX_OPEN_FILES;
    uint64_t generation = (fd - index) / MAX_OPEN_FILES;
    if (index >= MAX_OPEN_FILES) {
        return NULL;
    }
    struct oftable_slot *of = &oftable[index];
    if (generation < of->generation) {
        return NULL;
    }
    return of;
}

static fd_t of_to_fd(struct oftable_slot *of) {
    return (uint64_t)(of - oftable) + of->generation * MAX_OPEN_FILES;
}

int fd_alloc(fd_t *fd) {
    struct oftable_slot *of;
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

int fd_set_file(fd_t fd, void *file) {
    struct oftable_slot *of = fd_to_of(fd);
    if (of == NULL) {
        return -1;
    }
    return of_set_file(of, file);
}

int fd_set_dir(fd_t fd, void *dir) {
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

int fd_begin_op_file(fd_t fd, void **file) {
    struct oftable_slot *of = fd_to_of(fd);
    if (of == NULL) {
        return -1;
    }
    return of_begin_op(of, file);
}

int fd_begin_op_dir(fd_t fd, void **dir) {
    struct oftable_slot *of = fd_to_of(fd);
    if (of == NULL) {
        return -1;
    }
    return of_begin_op(of, dir);
}

void fd_end_op(fd_t fd) {
    struct oftable_slot *of = fd_to_of(fd);
    assert(of != NULL);
    int err = of_end_op(of);
    assert(err == 0);
}
