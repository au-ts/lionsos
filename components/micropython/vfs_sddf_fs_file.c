#include "py/mphal.h"
#include "py/mpthread.h"
#include "py/runtime.h"
#include "py/stream.h"
#include "fs_helpers.h"
#include "micropython.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

/* MicroPython will ask for a default buffer size to create a stream for when using a VFS. */
#define VFS_SDDF_FS_FILE_BUFFER_SIZE (FS_BUFFER_SIZE)

typedef struct _mp_obj_vfs_sddf_fs_file_t {
    mp_obj_base_t base;
    uint64_t fd;
    uint64_t pos;
} mp_obj_vfs_sddf_fs_file_t;

STATIC void vfs_sddf_fs_file_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    mp_obj_vfs_sddf_fs_file_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<io.%s %d>", mp_obj_get_type_str(self_in), self->fd);
}

STATIC mp_obj_t vfs_sddf_fs_file_fileno(mp_obj_t self_in) {
    mp_obj_vfs_sddf_fs_file_t *self = MP_OBJ_TO_PTR(self_in);
    // check_fd_is_open(self);
    return MP_OBJ_NEW_SMALL_INT(self->fd);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(vfs_sddf_fs_file_fileno_obj, vfs_sddf_fs_file_fileno);

STATIC mp_obj_t vfs_sddf_fs_file___exit__(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    return mp_stream_close(args[0]);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(vfs_sddf_fs_file___exit___obj, 4, 4, vfs_sddf_fs_file___exit__);

STATIC mp_uint_t vfs_sddf_fs_file_read(mp_obj_t o_in, void *buf, mp_uint_t size, int *errcode) {
    mp_obj_vfs_sddf_fs_file_t *o = MP_OBJ_TO_PTR(o_in);
    // check_fd_is_open(o);

    ptrdiff_t read_buffer;
    int err = fs_buffer_allocate(&read_buffer);
    if (err) {
        return MP_STREAM_ERROR;
    } 

    struct sddf_fs_completion completion;
    err = fs_command_blocking(&completion, SDDF_FS_CMD_PREAD, o->fd, read_buffer, size, o->pos);
    if (err || completion.status != 0) {
        fs_buffer_free(read_buffer);
        return MP_STREAM_ERROR;
    }

    memcpy(buf, fs_buffer_ptr(read_buffer), completion.data[0]);
    o->pos += completion.data[0];
    fs_buffer_free(read_buffer);

    return (mp_uint_t)completion.data[0];
}

STATIC mp_uint_t vfs_sddf_fs_file_write(mp_obj_t o_in, const void *buf, mp_uint_t size, int *errcode) {
    mp_obj_vfs_sddf_fs_file_t *o = MP_OBJ_TO_PTR(o_in);
    // check_fd_is_open(o);

    ptrdiff_t write_buffer;
    int err = fs_buffer_allocate(&write_buffer);
    if (err) {
        return MP_STREAM_ERROR;
    }

    memcpy(fs_buffer_ptr(write_buffer), buf, size);

    struct sddf_fs_completion completion;
    err = fs_command_blocking(&completion, SDDF_FS_CMD_PWRITE, o->fd, write_buffer, size, o->pos);
    fs_buffer_free(write_buffer);

    if (completion.status != 0) {
        return MP_STREAM_ERROR;
    }
    o->pos += completion.data[0];
    return (mp_uint_t)completion.data[0];
}

STATIC mp_uint_t vfs_sddf_fs_file_ioctl(mp_obj_t o_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    mp_obj_vfs_sddf_fs_file_t *o = MP_OBJ_TO_PTR(o_in);

    if (request != MP_STREAM_CLOSE) {
        // check_fd_is_open(o);
    }

    switch (request) {
        case MP_STREAM_FLUSH: {
            return 0;
        }
        case MP_STREAM_SEEK: {
            struct mp_stream_seek_t *s = (struct mp_stream_seek_t *)arg;
            o->pos = s->offset;
            // TODO: use s->whence
            return 0;
        }
        case MP_STREAM_CLOSE: {
            struct sddf_fs_completion completion;
            fs_command_blocking(&completion, SDDF_FS_CMD_CLOSE, o->fd, 0, 0, 0);
            return 0;
        }
        case MP_STREAM_GET_FILENO:
            return o->fd;
        #if MICROPY_PY_USELECT
        case MP_STREAM_POLL: {
            mp_raise_NotImplementedError(MP_ERROR_TEXT("poll on file not available"));
            break;
        }
        #endif
        case MP_STREAM_GET_BUFFER_SIZE: {
            return VFS_SDDF_FS_FILE_BUFFER_SIZE;
        }
        default:
            *errcode = EINVAL;
            return MP_STREAM_ERROR;
    }
}

STATIC const mp_rom_map_elem_t vfs_sddf_fs_rawfile_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_fileno), MP_ROM_PTR(&vfs_sddf_fs_file_fileno_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_readlines), MP_ROM_PTR(&mp_stream_unbuffered_readlines_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_seek), MP_ROM_PTR(&mp_stream_seek_obj) },
    { MP_ROM_QSTR(MP_QSTR_tell), MP_ROM_PTR(&mp_stream_tell_obj) },
    { MP_ROM_QSTR(MP_QSTR_flush), MP_ROM_PTR(&mp_stream_flush_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&mp_identity_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&vfs_sddf_fs_file___exit___obj) },
};

STATIC MP_DEFINE_CONST_DICT(vfs_sddf_fs_rawfile_locals_dict, vfs_sddf_fs_rawfile_locals_dict_table);

STATIC const mp_stream_p_t vfs_sddf_fs_fileio_stream_p = {
    .read = vfs_sddf_fs_file_read,
    .write = vfs_sddf_fs_file_write,
    .ioctl = vfs_sddf_fs_file_ioctl,
};

MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_vfs_sddf_fs_fileio,
    MP_QSTR_FileIO,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    print, vfs_sddf_fs_file_print,
    protocol, &vfs_sddf_fs_fileio_stream_p,
    locals_dict, &vfs_sddf_fs_rawfile_locals_dict
);

STATIC const mp_stream_p_t vfs_sddf_fs_textio_stream_p = {
    .read = vfs_sddf_fs_file_read,
    .write = vfs_sddf_fs_file_write,
    .ioctl = vfs_sddf_fs_file_ioctl,
    .is_text = true,
};

MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_vfs_sddf_fs_textio,
    MP_QSTR_TextIOWrapper,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    print, vfs_sddf_fs_file_print,
    protocol, &vfs_sddf_fs_textio_stream_p,
    locals_dict, &vfs_sddf_fs_rawfile_locals_dict
);

const mp_obj_vfs_sddf_fs_file_t mp_sys_stdin_obj = {{&mp_type_vfs_sddf_fs_textio}, STDIN_FILENO};
const mp_obj_vfs_sddf_fs_file_t mp_sys_stdout_obj = {{&mp_type_vfs_sddf_fs_textio}, STDOUT_FILENO};
const mp_obj_vfs_sddf_fs_file_t mp_sys_stderr_obj = {{&mp_type_vfs_sddf_fs_textio}, STDERR_FILENO};

mp_obj_t mp_vfs_sddf_fs_file_open(const mp_obj_type_t *type, mp_obj_t file_in, mp_obj_t mode_in) {
    mp_obj_vfs_sddf_fs_file_t *o = m_new_obj(mp_obj_vfs_sddf_fs_file_t);
    const char *mode_s = mp_obj_str_get_str(mode_in);

    uint64_t flags = 0;
    while (*mode_s) {
        switch (*mode_s++) {
            case 'r':
                flags |= SDDF_FS_OPEN_FLAGS_READ_ONLY;
                break;
            case 'w':
            case 'a':
                flags |= SDDF_FS_OPEN_FLAGS_WRITE_ONLY;
                flags |= SDDF_FS_OPEN_FLAGS_CREATE;
                break;
            case '+':
                flags |= SDDF_FS_OPEN_FLAGS_READ_WRITE;
                break;
            case 'b':
                type = &mp_type_vfs_sddf_fs_fileio;
                break;
            case 't':
                type = &mp_type_vfs_sddf_fs_textio;
                break;
        }
    }

    o->base.type = type;

    mp_obj_t fid = file_in;

    if (mp_obj_is_small_int(fid)) {
        o->fd = MP_OBJ_SMALL_INT_VALUE(fid);
        return MP_OBJ_FROM_PTR(o);
    }

    const char *fname = mp_obj_str_get_str(fid);

    ptrdiff_t path_buffer;
    int err = fs_buffer_allocate(&path_buffer);
    if (err) {
        mp_raise_OSError(err);
        return mp_const_none;
    }

    uint64_t path_len = strlen(fname) + 1;
    strcpy(fs_buffer_ptr(path_buffer), fname);

    struct sddf_fs_completion completion;
    fs_command_blocking(&completion, SDDF_FS_CMD_OPEN, path_buffer, path_len, flags, 0);

    fs_buffer_free(path_buffer);
    if (completion.status != 0) {
        mp_raise_OSError(completion.status);
        return mp_const_none;
    }
    o->fd = completion.data[0];
    return MP_OBJ_FROM_PTR(o);
}
