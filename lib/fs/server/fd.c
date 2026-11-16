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
    uint64_t client_id;
};

struct oftable_slot oftable[MAX_OPEN_FILES];

static int of_alloc(uint64_t client_id, struct oftable_slot **slot) {
    for (uint64_t i = 0; i < MAX_OPEN_FILES; i++) {
        struct oftable_slot *candidate = &oftable[i];
        if (candidate->state == state_free) {
            candidate->state = state_allocated;
            candidate->client_id = client_id;
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

static int of_begin_op_file(struct oftable_slot *of, void **file_handle_p) {
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

static int of_begin_op_dir(struct oftable_slot *of, void **dir_handle_p) {
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

static struct oftable_slot *fd_to_of_for_client(uint64_t client_id, fd_t fd) {
    uint64_t index = fd % MAX_OPEN_FILES;
    uint64_t generation = (fd - index) / MAX_OPEN_FILES;
    if (index >= MAX_OPEN_FILES) {
        return NULL;
    }
    struct oftable_slot *of = &oftable[index];
    // Reject stale or fabricated file descriptors
    if (generation != of->generation || client_id != of->client_id) {
        return NULL;
    }
    return of;
}

static fd_t of_to_fd(struct oftable_slot *of) {
    return (uint64_t)(of - oftable) + of->generation * MAX_OPEN_FILES;
}

int fd_alloc_for_client(uint64_t client_id, fd_t *fd) {
    struct oftable_slot *of;
    int err = of_alloc(client_id, &of);
    if (!err) {
        *fd = of_to_fd(of);
    }
    return err;
}

int fd_free_for_client(uint64_t client_id, fd_t fd) {
    struct oftable_slot *of = fd_to_of_for_client(client_id, fd);
    if (of == NULL) {
        return -1;
    }
    return of_free(of);
}

int fd_set_file_for_client(uint64_t client_id, fd_t fd, void *file) {
    struct oftable_slot *of = fd_to_of_for_client(client_id, fd);
    if (of == NULL) {
        return -1;
    }
    return of_set_file(of, file);
}

int fd_set_dir_for_client(uint64_t client_id, fd_t fd, void *dir) {
    struct oftable_slot *of = fd_to_of_for_client(client_id, fd);
    if (of == NULL) {
        return -1;
    }
    return of_set_dir(of, dir);
}

int fd_unset_for_client(uint64_t client_id, fd_t fd) {
    struct oftable_slot *of = fd_to_of_for_client(client_id, fd);
    if (of == NULL) {
        return -1;
    }
    return of_unset(of);
}

int fd_begin_op_file_for_client(uint64_t client_id, fd_t fd, void **file) {
    struct oftable_slot *of = fd_to_of_for_client(client_id, fd);
    if (of == NULL) {
        return -1;
    }
    return of_begin_op_file(of, file);
}

int fd_begin_op_dir_for_client(uint64_t client_id, fd_t fd, void **dir) {
    struct oftable_slot *of = fd_to_of_for_client(client_id, fd);
    if (of == NULL) {
        return -1;
    }
    return of_begin_op_dir(of, dir);
}

void fd_end_op_for_client(uint64_t client_id, fd_t fd) {
    struct oftable_slot *of = fd_to_of_for_client(client_id, fd);
    assert(of != NULL);
    int err = of_end_op(of);
    assert(err == 0);
}

int fd_alloc(fd_t *fd) {
    return fd_alloc_for_client(0, fd);
}

int fd_free(fd_t fd) {
    return fd_free_for_client(0, fd);
}

int fd_set_file(fd_t fd, void *file) {
    return fd_set_file_for_client(0, fd, file);
}

int fd_set_dir(fd_t fd, void *dir) {
    return fd_set_dir_for_client(0, fd, dir);
}

int fd_unset(fd_t fd) {
    return fd_unset_for_client(0, fd);
}

int fd_begin_op_file(fd_t fd, void **file) {
    return fd_begin_op_file_for_client(0, fd, file);
}

int fd_begin_op_dir(fd_t fd, void **dir) {
    return fd_begin_op_dir_for_client(0, fd, dir);
}

void fd_end_op(fd_t fd) {
    fd_end_op_for_client(0, fd);
}
