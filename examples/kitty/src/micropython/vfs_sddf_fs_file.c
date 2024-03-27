#include "py/mphal.h"
#include "py/mpthread.h"
#include "py/runtime.h"
#include "py/stream.h"
#include "sddf_fs.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

/* MicroPython will ask for a default buffer size to create a stream for when using a VFS. */
#define VFS_SDDF_FS_FILE_BUFFER_SIZE (1024)

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

    struct read_response response = sddf_fs_pread(o->fd, size, o->pos);
    if (response.status < 0) {
        return MP_STREAM_ERROR;
    }
    o->pos += response.len;
    memcpy(buf, response.data, response.len);
    return (mp_uint_t)response.len;
}

STATIC mp_uint_t vfs_sddf_fs_file_write(mp_obj_t o_in, const void *buf, mp_uint_t size, int *errcode) {
    mp_obj_vfs_sddf_fs_file_t *o = MP_OBJ_TO_PTR(o_in);
    // check_fd_is_open(o);

    int response = sddf_fs_pwrite(o->fd, buf, size, o->pos);
    if (response <= 0) {
        return MP_STREAM_ERROR;
    }
    o->pos += response;
    return (mp_uint_t)response;
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
            mp_raise_NotImplementedError(MP_ERROR_TEXT("seek on file not available"));
            return 0;
        }
        case MP_STREAM_CLOSE:
            sddf_fs_close(o->fd);
            return 0;
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

    int mode_rw = 0, mode_x = 0;
    while (*mode_s) {
        switch (*mode_s++) {
            case 'r':
                mode_rw = O_RDONLY;
                break;
            case 'w':
                mode_rw = O_WRONLY;
                mode_x = O_CREAT | O_TRUNC;
                break;
            case 'a':
                mode_rw = O_WRONLY;
                mode_x = O_CREAT | O_APPEND;
                break;
            case '+':
                mode_rw = O_RDWR;
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
    struct open_response response = sddf_fs_open(fname);
    if (response.status != 0) {
        mp_raise_OSError(response.status);
        return mp_const_none;
    }
    o->fd = response.fd;
    return MP_OBJ_FROM_PTR(o);
}
