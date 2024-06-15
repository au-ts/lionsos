#pragma once

#include "py/lexer.h"
#include "py/obj.h"

extern const mp_obj_type_t mp_type_vfs_fs;
extern const mp_obj_type_t mp_type_vfs_fs_fileio;
extern const mp_obj_type_t mp_type_vfs_fs_textio;

mp_obj_t mp_vfs_fs_file_open(const mp_obj_type_t *type, mp_obj_t file_in, mp_obj_t mode_in);
