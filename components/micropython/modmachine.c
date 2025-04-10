/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include <stdio.h>

#include "micropython.h"
#include "modmachine.h"
#include "extmod/modmachine.h"

#ifdef MICROPY_PY_MACHINE

static const mp_rom_map_elem_t machine_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_machine) },
    { MP_ROM_QSTR(MP_QSTR_I2C), MP_ROM_PTR(&machine_i2c_type) },
};

static MP_DEFINE_CONST_DICT(machine_module_globals, machine_module_globals_table);

const mp_obj_module_t mp_module_machine = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&machine_module_globals,
};

MP_REGISTER_EXTENSIBLE_MODULE(MP_QSTR_machine, mp_module_machine);

#endif /* MICROPY_PY_MACHINE */
