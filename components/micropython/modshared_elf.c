/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <sddf/util/cache.h>
#include "py/runtime.h"
#include "micropython.h"
#include <lions/shared_elf/shared_elf.h>

#include <sddf/util/printf.h>

#include "../include/arena.h" // this will need to change obviously

extern void *Shared_Elf_Arena;
/*
 * We get notified when we *can* write to the elfbuffer, meaning that MicroPython
 * needs to wait until the elfbuffer is ready.
 */
static mp_obj_t shared_elf_wait(void) {
    arena_init(Shared_Elf_Arena, 0x2000000);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(shared_elf_wait_obj, shared_elf_wait);

static int pd; // which pd we are interested in

int fill_data_size;

static mp_obj_t machine_shared_elf_send(mp_obj_t arg0, mp_obj_t arg1, mp_obj_t arg2) {
    if (mp_obj_get_int(arg2) < 0) {
        sddf_dprintf("boutta reload\n");

        microkit_msginfo msg = microkit_msginfo_new(0, 3);
        microkit_mr_set(0, mp_obj_get_int(arg0)); // pd id
        microkit_mr_set(1, mp_obj_get_int(arg1)); // vaddr of the passive symbol
        microkit_mr_set(2, (-1) * mp_obj_get_int(arg2)); // entry point since was neg before
        microkit_ppcall(SHARED_ELF_VMM_CH, msg);

        sddf_dprintf("finished reload\n");
        seL4_DebugDumpScheduler();
        arena_reset(Shared_Elf_Arena);
        return mp_const_none;
    }

    int size = mp_obj_get_int(arg1);
    uint64_t target_vaddr = mp_obj_get_int(arg2); // hopefully this is 64 bit?

    char *elfbuffer = arena_alloc(Shared_Elf_Arena, size, target_vaddr);

    mp_buffer_info_t bufinfo;
    mp_get_buffer(arg0, &bufinfo, MP_BUFFER_READ);
    uint8_t *buf = (uint8_t *)bufinfo.buf;

    for (int i = 0; i < size; i++) {
        elfbuffer[i] = buf[i];
    }

    cache_clean((unsigned long)elfbuffer, (unsigned long)elfbuffer + size);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(machine_shared_elf_send_obj, machine_shared_elf_send);

static const mp_rom_map_elem_t shared_elf_module_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_shared_elf) },
    { MP_ROM_QSTR(MP_QSTR_wait), MP_ROM_PTR(&shared_elf_wait_obj) },
    { MP_ROM_QSTR(MP_QSTR_machine_shared_elf_send), MP_ROM_PTR(&machine_shared_elf_send_obj) },
};
static MP_DEFINE_CONST_DICT(shared_elf_module_globals, shared_elf_module_globals_table);

const mp_obj_module_t shared_elf_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&shared_elf_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_shared_elf, shared_elf_module);
