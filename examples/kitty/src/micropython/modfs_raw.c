#include <microkit.h>
#include "py/runtime.h"
#include "micropython.h"
#include "fs_helpers.h"
#include <fcntl.h>
#include <string.h>

mp_obj_t request_flags[SDDF_FS_QUEUE_CAPACITY];

void fs_request_flag_set(uint64_t request_id) {
    mp_obj_t flag = request_flags[request_id];
    if (flag != NULL) {
        mp_obj_t set_method[2];
        mp_load_method(flag, MP_QSTR_set, &set_method);
        mp_call_method_n_kw(0, 0, set_method);
    }
    request_flags[request_id] = NULL;
}

STATIC mp_obj_t request_open(mp_obj_t path_in, mp_obj_t flag_in) {
    const char *path = mp_obj_str_get_str(path_in);

    uint64_t request_id;
    int err = fs_request_allocate(&request_id);
    if (err) {
        mp_raise_OSError(err);
        return mp_const_none;
    }

    ptrdiff_t path_buffer;
    err = fs_buffer_allocate(&path_buffer);
    if (err) {
        fs_request_free(request_id);
        mp_raise_OSError(err);
        return mp_const_none;
    }

    uint64_t path_len = strlen(path) + 1;
    strcpy(fs_buffer_ptr(path_buffer), path);

    request_flags[request_id] = flag_in;
    fs_command_issue(request_id, SDDF_FS_CMD_OPEN, path_buffer, path_len, O_RDONLY, 0644);

    return mp_obj_new_int_from_uint(request_id);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(request_open_obj, request_open);

STATIC mp_obj_t complete_open(mp_obj_t request_id_in) {
    uint64_t request_id = mp_obj_get_int(request_id_in);

    struct sddf_fs_command command;
    struct sddf_fs_completion completion;
    fs_command_complete(request_id, &command, &completion);

    fs_buffer_free(command.args[0]);
    fs_request_free(request_id);

    if (completion.status) {
        mp_raise_OSError(completion.status);
        return mp_const_none;
    }
    return mp_obj_new_int_from_uint(completion.data[0]);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(complete_open_obj, complete_open);

STATIC mp_obj_t request_close(mp_obj_t fd_in, mp_obj_t flag_in) {
    uint64_t fd = mp_obj_get_int(fd_in);

    uint64_t request_id;
    int err = fs_request_allocate(&request_id);
    if (err) {
        mp_raise_OSError(err);
        return mp_const_none;
    }
    request_flags[request_id] = flag_in;
    fs_command_issue(request_id, SDDF_FS_CMD_CLOSE, fd, 0, 0, 0);
    return mp_obj_new_int_from_uint(request_id);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(request_close_obj, request_close);

STATIC mp_obj_t complete_close(mp_obj_t request_id_in) {
    uint64_t request_id = mp_obj_get_int(request_id_in);

    struct sddf_fs_command command;
    struct sddf_fs_completion completion;
    fs_command_complete(request_id, &command, &completion);

    fs_request_free(request_id);

    return mp_obj_new_int_from_uint(completion.status);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(complete_close_obj, complete_close);

STATIC mp_obj_t request_pread(mp_uint_t n_args, const mp_obj_t *args) {
    uint64_t fd = mp_obj_get_int(args[0]);
    uint64_t nbyte = mp_obj_get_int(args[1]);
    uint64_t offset = mp_obj_get_int(args[2]);
    mp_obj_t flag = args[3];

    ptrdiff_t read_buffer;
    int err = fs_buffer_allocate(&read_buffer);
    if (err) {
        mp_raise_OSError(err);
        return mp_const_none;
    } 

    uint64_t request_id;
    err = fs_request_allocate(&request_id);
    if (err) {
        fs_buffer_free(read_buffer);
        mp_raise_OSError(err);
        return mp_const_none;
    }

    request_flags[request_id] = flag;
    fs_command_issue(request_id, SDDF_FS_CMD_PREAD, fd, read_buffer, nbyte, offset);
    return mp_obj_new_int_from_uint(request_id);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(request_pread_obj, 4, 4, request_pread);

STATIC mp_obj_t complete_pread(mp_obj_t request_id_in) {
    uint64_t request_id = mp_obj_get_int(request_id_in);

    struct sddf_fs_command command;
    struct sddf_fs_completion completion;
    fs_command_complete(request_id, &command, &completion);
    fs_request_free(request_id);

    mp_obj_t ret = mp_obj_new_bytes(fs_buffer_ptr(command.args[1]), completion.data[0]);
    fs_buffer_free(command.args[1]);
    return ret;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(complete_pread_obj, complete_pread);

STATIC mp_obj_t request_stat(mp_obj_t path_in, mp_obj_t flag_in) {
    const char *path = mp_obj_str_get_str(path_in);

    uint64_t request_id;
    int err = fs_request_allocate(&request_id);
    if (err) {
        mp_raise_OSError(err);
        return mp_const_none;
    }

    ptrdiff_t path_buffer;
    err = fs_buffer_allocate(&path_buffer);
    if (err) {
        fs_request_free(request_id);
        mp_raise_OSError(err);
        return mp_const_none;
    }

    ptrdiff_t output_buffer;
    err = fs_buffer_allocate(&output_buffer);
    if (err) {
        fs_request_free(request_id);
        fs_buffer_free(path_buffer);
        mp_raise_OSError(err);
        return mp_const_none;
    }

    uint64_t path_len = strlen(path) + 1;
    strcpy(fs_buffer_ptr(path_buffer), path);

    request_flags[request_id] = flag_in;
    fs_command_issue(request_id, SDDF_FS_CMD_STAT, path_buffer, path_len, output_buffer, 0);
    return mp_obj_new_int_from_uint(request_id);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(request_stat_obj, request_stat);

STATIC mp_obj_t complete_stat(mp_obj_t request_id_in) {
    uint64_t request_id = mp_obj_get_int(request_id_in);

    struct sddf_fs_command command;
    struct sddf_fs_completion completion;
    fs_command_complete(request_id, &command, &completion);
    fs_request_free(request_id);
    fs_buffer_free(command.args[0]);

    if (completion.status) {
        mp_raise_OSError(completion.status);
        return mp_const_none;
    }

    struct sddf_fs_stat_64 *sb = (struct sddf_fs_stat_64 *)fs_buffer_ptr(command.args[2]);
    mp_obj_tuple_t *t = MP_OBJ_TO_PTR(mp_obj_new_tuple(10, NULL));
    t->items[0] = MP_OBJ_NEW_SMALL_INT(sb->mode);
    t->items[1] = mp_obj_new_int_from_uint(sb->ino);
    t->items[2] = mp_obj_new_int_from_uint(sb->dev);
    t->items[3] = mp_obj_new_int_from_uint(sb->nlink);
    t->items[4] = mp_obj_new_int_from_uint(sb->uid);
    t->items[5] = mp_obj_new_int_from_uint(sb->gid);
    t->items[6] = mp_obj_new_int_from_uint(sb->size);
    t->items[7] = mp_obj_new_int_from_uint(sb->atime);
    t->items[8] = mp_obj_new_int_from_uint(sb->mtime);
    t->items[9] = mp_obj_new_int_from_uint(sb->ctime);
    fs_buffer_free(command.args[2]);

    return MP_OBJ_FROM_PTR(t);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(complete_stat_obj, complete_stat);

STATIC const mp_rom_map_elem_t fs_raw_module_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_fs_raw) },
    { MP_ROM_QSTR(MP_QSTR_request_open), MP_ROM_PTR(&request_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_complete_open), MP_ROM_PTR(&complete_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_request_close), MP_ROM_PTR(&request_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_complete_close), MP_ROM_PTR(&complete_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_request_pread), MP_ROM_PTR(&request_pread_obj) },
    { MP_ROM_QSTR(MP_QSTR_complete_pread), MP_ROM_PTR(&complete_pread_obj) },
    { MP_ROM_QSTR(MP_QSTR_request_stat), MP_ROM_PTR(&request_stat_obj) },
    { MP_ROM_QSTR(MP_QSTR_complete_stat), MP_ROM_PTR(&complete_stat_obj) },
};
STATIC MP_DEFINE_CONST_DICT(fs_raw_module_globals, fs_raw_module_globals_table);

const mp_obj_module_t fs_raw_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&fs_raw_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_fs_raw, fs_raw_module);
