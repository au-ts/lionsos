/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/mpthread.h"
#include "py/builtin.h"
#include "extmod/vfs.h"
#include "vfs_fs.h"
#include "micropython.h"
#include "fs_helpers.h"

#include <stdio.h>
#include <string.h>

typedef struct _mp_obj_vfs_fs_t {
    mp_obj_base_t base;
    vstr_t root;
    size_t root_len;
    bool readonly;
} mp_obj_vfs_fs_t;

STATIC const char *vfs_fs_get_path_str(mp_obj_vfs_fs_t *self, mp_obj_t path) {
    if (self->root_len == 0) {
        return mp_obj_str_get_str(path);
    } else {
        self->root.len = self->root_len;
        vstr_add_str(&self->root, mp_obj_str_get_str(path));
        return vstr_null_terminated_str(&self->root);
    }
}

STATIC mp_obj_t vfs_fs_get_path_obj(mp_obj_vfs_fs_t *self, mp_obj_t path) {
    if (self->root_len == 0) {
        return path;
    } else {
        self->root.len = self->root_len;
        vstr_add_str(&self->root, mp_obj_str_get_str(path));
        return mp_obj_new_str(self->root.buf, self->root.len);
    }
}

STATIC mp_import_stat_t mp_vfs_fs_import_stat(void *self_in, const char *path) {
    mp_obj_vfs_fs_t *self = self_in;
    if (self->root_len != 0) {
        self->root.len = self->root_len;
        vstr_add_str(&self->root, path);
        path = vstr_null_terminated_str(&self->root);
    }

    ptrdiff_t path_buffer;
    int err = fs_buffer_allocate(&path_buffer);
    assert(!err);

    ptrdiff_t output_buffer;
    err = fs_buffer_allocate(&output_buffer);
    assert(!err);

    uint64_t path_len = strlen(path);
    memcpy(fs_buffer_ptr(path_buffer), path, path_len);

    fs_cmpl_t completion;
    fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_STAT,
        .params.stat = {
            .path.offset = path_buffer,
            .path.size = path_len,
            .buf.offset = output_buffer,
            .buf.size = FS_BUFFER_SIZE,
        }
    });

    fs_stat_t stat;
    memcpy(&stat, fs_buffer_ptr(output_buffer), sizeof stat);

    fs_buffer_free(output_buffer);
    fs_buffer_free(path_buffer);

    if (completion.status != FS_STATUS_SUCCESS) {
        return MP_IMPORT_STAT_NO_EXIST;
    } else if (stat.mode & 0040000) { // TODO name constant
        return MP_IMPORT_STAT_DIR;
    } else {
        return MP_IMPORT_STAT_FILE;
    }
}

STATIC mp_obj_t vfs_fs_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 1, false);

    mp_obj_vfs_fs_t *vfs = mp_obj_malloc(mp_obj_vfs_fs_t, type);
    vstr_init(&vfs->root, 0);
    if (n_args == 1) {
        vstr_add_str(&vfs->root, mp_obj_str_get_str(args[0]));
        vstr_add_char(&vfs->root, '/');
    }
    vfs->root_len = vfs->root.len;
    vfs->readonly = false;

    return MP_OBJ_FROM_PTR(vfs);
}

STATIC mp_obj_t vfs_fs_mount(mp_obj_t self_in, mp_obj_t readonly, mp_obj_t mkfs) {
    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t){ .type = FS_CMD_INITIALISE });
    if (err || completion.status != FS_STATUS_SUCCESS) {
        mp_raise_OSError(1);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(vfs_fs_mount_obj, vfs_fs_mount);

STATIC mp_obj_t vfs_fs_umount(mp_obj_t self_in) {
    (void)self_in;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(vfs_fs_umount_obj, vfs_fs_umount);

STATIC mp_obj_t vfs_fs_open(mp_obj_t self_in, mp_obj_t path_in, mp_obj_t mode_in) {
    mp_obj_vfs_fs_t *self = MP_OBJ_TO_PTR(self_in);
    const char *mode = mp_obj_str_get_str(mode_in);
    if (self->readonly
        && (strchr(mode, 'w') != NULL || strchr(mode, 'a') != NULL || strchr(mode, '+') != NULL)) {
        mp_raise_OSError(MP_EROFS);
    }
    if (!mp_obj_is_small_int(path_in)) {
        path_in = vfs_fs_get_path_obj(self, path_in);
    }
    return mp_vfs_fs_file_open(&mp_type_vfs_fs_textio, path_in, mode_in);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(vfs_fs_open_obj, vfs_fs_open);

STATIC mp_obj_t vfs_fs_chdir(mp_obj_t self_in, mp_obj_t path_in) {
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(vfs_fs_chdir_obj, vfs_fs_chdir);

STATIC mp_obj_t vfs_fs_getcwd(mp_obj_t self_in) {
    return mp_obj_new_str("/", strlen("/"));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(vfs_fs_getcwd_obj, vfs_fs_getcwd);

typedef struct _vfs_fs_ilistdir_it_t {
    mp_obj_base_t base;
    mp_fun_1_t iternext;
    bool is_str;
    uint64_t dir;
} vfs_fs_ilistdir_it_t;

STATIC mp_obj_t vfs_fs_ilistdir_it_iternext(mp_obj_t self_in) {
    vfs_fs_ilistdir_it_t *self = MP_OBJ_TO_PTR(self_in);

    for (;;) {
        ptrdiff_t name_buffer;
        int err = fs_buffer_allocate(&name_buffer);
        assert(!err);

        fs_cmpl_t completion;
        fs_command_blocking(&completion, (fs_cmd_t){
            .type = FS_CMD_READDIR,
            .params.readdir = {
                .fd = self->dir,
                .buf.offset = name_buffer,
                .buf.size = FS_BUFFER_SIZE,
            }
        });

        if (completion.status != FS_STATUS_SUCCESS) {
            fs_buffer_free(name_buffer);
            break;
        }

        const char *fn = fs_buffer_ptr(name_buffer);

        if (fn[0] == '.' && (fn[1] == 0 || fn[1] == '.')) {
            // skip . and ..
            fs_buffer_free(name_buffer);
            continue;
        }

        // make 3-tuple with info about this entry
        mp_obj_tuple_t *t = MP_OBJ_TO_PTR(mp_obj_new_tuple(3, NULL));

        if (self->is_str) {
            t->items[0] = mp_obj_new_str(fn, strlen(fn));
        } else {
            t->items[0] = mp_obj_new_bytes((const byte *)fn, strlen(fn));
        }
        t->items[1] = MP_OBJ_NEW_SMALL_INT(0);
        t->items[2] = MP_OBJ_NEW_SMALL_INT(0);

        fs_buffer_free(name_buffer);

        return MP_OBJ_FROM_PTR(t);
    }

    fs_cmpl_t completion;
    fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_CLOSEDIR,
        .params.closedir.fd = self->dir,
    });
    self->dir = 0;
    return MP_OBJ_STOP_ITERATION;
}

STATIC mp_obj_t vfs_fs_ilistdir(mp_obj_t self_in, mp_obj_t path_in) {
    mp_obj_vfs_fs_t *self = MP_OBJ_TO_PTR(self_in);
    vfs_fs_ilistdir_it_t *iter = mp_obj_malloc(vfs_fs_ilistdir_it_t, &mp_type_polymorph_iter);
    iter->iternext = vfs_fs_ilistdir_it_iternext;
    iter->is_str = mp_obj_get_type(path_in) == &mp_type_str;
    const char *path = vfs_fs_get_path_str(self, path_in);
    if (path[0] == '\0') {
        path = ".";
    }

    ptrdiff_t path_buffer;
    int err = fs_buffer_allocate(&path_buffer);
    if (err) {
        mp_raise_OSError(err);
        return mp_const_none;
    }
    uint64_t path_len = strlen(path);
    memcpy(fs_buffer_ptr(path_buffer), path, path_len);

    fs_cmpl_t completion;
    err = fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_OPENDIR,
        .params.opendir = {
            .path.offset = path_buffer,
            .path.size = path_len,
        }
    });
    fs_buffer_free(path_buffer);
    if (err) {
        mp_raise_OSError(err);
        return mp_const_none;
    }

    if (completion.status != FS_STATUS_SUCCESS) {
        mp_raise_OSError(completion.status);
        return mp_const_none;
    }
    iter->dir = completion.data.opendir.fd;
    return MP_OBJ_FROM_PTR(iter);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(vfs_fs_ilistdir_obj, vfs_fs_ilistdir);

typedef struct _mp_obj_listdir_t {
    mp_obj_base_t base;
    mp_fun_1_t iternext;
    uint64_t dir;
} mp_obj_listdir_t;

STATIC mp_obj_t vfs_fs_mkdir(mp_obj_t self_in, mp_obj_t path_in) {
    mp_obj_vfs_fs_t *self = MP_OBJ_TO_PTR(self_in);
    const char *path = vfs_fs_get_path_str(self, path_in);

    ptrdiff_t path_buffer;
    int err = fs_buffer_allocate(&path_buffer);
    if (err) {
        mp_raise_OSError(err);
        return mp_const_none;
    }

    uint64_t path_len = strlen(path);
    memcpy(fs_buffer_ptr(path_buffer), path, path_len);
    
    fs_cmpl_t completion;
    err = fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_MKDIR,
        .params.mkdir = {
            .path.offset = path_buffer,
            .path.size = path_len,
        }
    });
    if (err) {
        fs_buffer_free(path_buffer);
        mp_raise_OSError(err);
    }

    fs_buffer_free(path_buffer);

    if (completion.status != FS_STATUS_SUCCESS) {
        mp_raise_OSError(completion.status);
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(vfs_fs_mkdir_obj, vfs_fs_mkdir);

STATIC mp_obj_t vfs_fs_remove(mp_obj_t self_in, mp_obj_t path_in) {
    mp_obj_vfs_fs_t *self = MP_OBJ_TO_PTR(self_in);
    const char *path = vfs_fs_get_path_str(self, path_in);

    ptrdiff_t path_buffer;
    int err = fs_buffer_allocate(&path_buffer);
    if (err) {
        mp_raise_OSError(err);
        return mp_const_none;
    }

    uint64_t path_len = strlen(path);
    memcpy(fs_buffer_ptr(path_buffer), path, path_len);

    fs_cmpl_t completion;
    err = fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_UNLINK,
        .params.unlink = {
            .path.offset = path_buffer,
            .path.size = path_len,
        }
    });

    fs_buffer_free(path_buffer);

    if (completion.status != FS_STATUS_SUCCESS) {
        mp_raise_OSError(completion.status);
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(vfs_fs_remove_obj, vfs_fs_remove);

STATIC mp_obj_t vfs_fs_rename(mp_obj_t self_in, mp_obj_t old_path_in, mp_obj_t new_path_in) {
    mp_obj_vfs_fs_t *self = MP_OBJ_TO_PTR(self_in);
    const char *old_path = vfs_fs_get_path_str(self, old_path_in);
    const char *new_path = vfs_fs_get_path_str(self, new_path_in);

    ptrdiff_t old_path_buffer;
    int err = fs_buffer_allocate(&old_path_buffer);
    if (err) {
        mp_raise_OSError(err);
    }

    ptrdiff_t new_path_buffer;
    err = fs_buffer_allocate(&new_path_buffer);
    if (err) {
        fs_buffer_free(old_path_buffer);
        mp_raise_OSError(err);
    }

    uint64_t old_path_len = strlen(old_path);
    uint64_t new_path_len = strlen(new_path);

    memcpy(fs_buffer_ptr(old_path_buffer), old_path, old_path_len);
    memcpy(fs_buffer_ptr(new_path_buffer), new_path, new_path_len);

    fs_cmpl_t completion;
    fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_RENAME,
        .params.rename = {
            .old_path.offset = old_path_buffer,
            .old_path.size = old_path_len,
            .new_path.offset = new_path_buffer,
            .new_path.size = new_path_len,
        }
    });

    fs_buffer_free(old_path_buffer);
    fs_buffer_free(new_path_buffer);

    if (completion.status != FS_STATUS_SUCCESS) {
        mp_raise_OSError(completion.status);
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(vfs_fs_rename_obj, vfs_fs_rename);

STATIC mp_obj_t vfs_fs_rmdir(mp_obj_t self_in, mp_obj_t path_in) {
    mp_obj_vfs_fs_t *self = MP_OBJ_TO_PTR(self_in);
    const char *path = vfs_fs_get_path_str(self, path_in);
    
    ptrdiff_t path_buffer;
    int err = fs_buffer_allocate(&path_buffer);
    if (err) {
        mp_raise_OSError(err);
        return mp_const_none;
    }

    uint64_t path_len = strlen(path);
    memcpy(fs_buffer_ptr(path_buffer), path, path_len);

    fs_cmpl_t completion;
    fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_RMDIR,
        .params.rmdir = {
            .path.offset = path_buffer,
            .path.size = path_len,
        }
    });

    fs_buffer_free(path_buffer);

    if (completion.status != FS_STATUS_SUCCESS) {
        mp_raise_OSError(completion.status);
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(vfs_fs_rmdir_obj, vfs_fs_rmdir);

STATIC mp_obj_t vfs_fs_stat(mp_obj_t self_in, mp_obj_t path_in) {
    mp_obj_vfs_fs_t *self = MP_OBJ_TO_PTR(self_in);

    const char *path = vfs_fs_get_path_str(self, path_in);

    ptrdiff_t path_buffer;
    int err = fs_buffer_allocate(&path_buffer);
    assert(!err);

    ptrdiff_t output_buffer;
    err = fs_buffer_allocate(&output_buffer);
    assert(!err);

    uint64_t path_len = strlen(path);
    memcpy(fs_buffer_ptr(path_buffer), path, path_len);

    fs_cmpl_t completion;
    fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_STAT,
        .params.stat = {
            .path.offset = path_buffer,
            .path.size = path_len,
            .buf.offset = output_buffer,
            .buf.size = FS_BUFFER_SIZE,
        }
    });

    fs_stat_t sb;
    memcpy(&sb, fs_buffer_ptr(output_buffer), sizeof sb);

    fs_buffer_free(output_buffer);
    fs_buffer_free(path_buffer);

    if (completion.status != FS_STATUS_SUCCESS) {
        mp_raise_OSError(completion.status);
        return mp_const_none;
    }

    mp_obj_tuple_t *t = MP_OBJ_TO_PTR(mp_obj_new_tuple(10, NULL));
    t->items[0] = MP_OBJ_NEW_SMALL_INT(sb.mode);
    t->items[1] = mp_obj_new_int_from_uint(sb.ino);
    t->items[2] = mp_obj_new_int_from_uint(sb.dev);
    t->items[3] = mp_obj_new_int_from_uint(sb.nlink);
    t->items[4] = mp_obj_new_int_from_uint(sb.uid);
    t->items[5] = mp_obj_new_int_from_uint(sb.gid);
    t->items[6] = mp_obj_new_int_from_uint(sb.size);
    t->items[7] = mp_obj_new_int_from_uint(sb.atime);
    t->items[8] = mp_obj_new_int_from_uint(sb.mtime);
    t->items[9] = mp_obj_new_int_from_uint(sb.ctime);
    return MP_OBJ_FROM_PTR(t);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(vfs_fs_stat_obj, vfs_fs_stat);

STATIC const mp_rom_map_elem_t vfs_fs_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_mount), MP_ROM_PTR(&vfs_fs_mount_obj) },
    { MP_ROM_QSTR(MP_QSTR_umount), MP_ROM_PTR(&vfs_fs_umount_obj) },
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&vfs_fs_open_obj) },

    { MP_ROM_QSTR(MP_QSTR_chdir), MP_ROM_PTR(&vfs_fs_chdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_getcwd), MP_ROM_PTR(&vfs_fs_getcwd_obj) },
    { MP_ROM_QSTR(MP_QSTR_ilistdir), MP_ROM_PTR(&vfs_fs_ilistdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_mkdir), MP_ROM_PTR(&vfs_fs_mkdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_remove), MP_ROM_PTR(&vfs_fs_remove_obj) },
    { MP_ROM_QSTR(MP_QSTR_rename), MP_ROM_PTR(&vfs_fs_rename_obj) },
    { MP_ROM_QSTR(MP_QSTR_rmdir), MP_ROM_PTR(&vfs_fs_rmdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_stat), MP_ROM_PTR(&vfs_fs_stat_obj) },
    // #if MICROPY_PY_UOS_STATVFS
    // { MP_ROM_QSTR(MP_QSTR_statvfs), MP_ROM_PTR(&vfs_fs_statvfs_obj) },
    // #endif
};
STATIC MP_DEFINE_CONST_DICT(vfs_fs_locals_dict, vfs_fs_locals_dict_table);

STATIC const mp_vfs_proto_t vfs_fs_proto = {
    .import_stat = mp_vfs_fs_import_stat,
};

MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_vfs_fs,
    MP_QSTR_VfsSddf,
    MP_TYPE_FLAG_NONE,
    make_new, vfs_fs_make_new,
    protocol, &vfs_fs_proto,
    locals_dict, &vfs_fs_locals_dict
);
