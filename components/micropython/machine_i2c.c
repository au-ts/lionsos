/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <sddf/i2c/queue.h>
#include <sddf/i2c/config.h>
#include <sddf/i2c/client.h>
#include <sddf/i2c/libi2c.h>
#include <string.h>
#include "micropython.h"

#include "extmod/modmachine.h"
#include "modmachine.h"
#include "py/runtime.h"
#include "py/mperrno.h"

#if MICROPY_PY_MACHINE_I2C && MICROPY_HW_ENABLE_HW_I2C

extern bool i2c_enabled;
extern i2c_client_config_t i2c_config;
extern i2c_queue_handle_t i2c_queue_handle;
extern libi2c_conf_t libi2c_conf;

#define I2C_AVAILABLE_BUSES 1
#define I2C_MAX_BUSES 4
mp_int_t permitted_buses[I2C_AVAILABLE_BUSES] = {1};

typedef struct _machine_i2c_obj_t {
    mp_obj_base_t base;
    size_t port;
} machine_i2c_obj_t;

machine_i2c_obj_t i2c_bus_objs[I2C_MAX_BUSES] = {};
uint64_t meta_alloc = 0;

#define I2C_DEFAULT_TIMEOUT_US (50000) // 50ms

int mp_i2c_read(machine_i2c_obj_t *self, uint16_t addr, uint8_t *buf, size_t len, bool stop) {
    /* TODO: this code makes assumptions about there being only a single i2c device */

    // In theory, this function can never be called multiple times and left in flight from the same
    // micropython instance. Split meta region into 16 pieces anyway to prevent issues from weird
    // usage of this function, in case it somehow is called too many times.
    if (meta_alloc >= 16) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("i2c_read: I2C(%d)'s buffer memory exhausted"), self->port);
        return -MP_ENOMEM;
    }

    if (addr > (1<<7)) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("i2c_read: only 7-bit i2c addresses are supported!"),
                          self->port);
    }

    // If we run into this, we should write a proper implementation of machine_i2c_transfer
    if (len > 4096) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("i2c_read: read length too long!"), self->port);
        return -1;
    }
    meta_alloc++;
    uint8_t *i2c_meta = (uint8_t *)(i2c_config.meta.vaddr + (meta_alloc*(i2c_config.meta.size / 16)));


    uint8_t flag_mask = stop ? (I2C_FLAG_STOP | I2C_FLAG_READ) : I2C_FLAG_READ;
    int ret = i2c_dispatch(&libi2c_conf, (i2c_addr_t)addr, (void *)i2c_meta, (uint16_t)len, flag_mask);
    if (ret) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("i2c_read: libi2c failed to dispatch!"), self->port);
        return -MP_ENOMEM;
    }
    // Copy out read data
    memcpy(buf, i2c_meta, len);
    meta_alloc--;

    return 0;
}

int mp_i2c_write(machine_i2c_obj_t *self, uint16_t addr, uint8_t *buf, size_t len) {
    if (meta_alloc >= 16) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("i2c_read: I2C(%d)'s buffer memory exhausted"), self->port);
        return -MP_ENOMEM;
    }

    if (addr > (1<<7)) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("i2c_read: only 7-bit i2c addresses are supported!"),
                          self->port);
    }

    // If we run into this, we should write a proper implementation of machine_i2c_transfer
    if (len > 4096) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("i2c_read: read length too long!"), self->port);
        return -1;
    }
    meta_alloc++;
    uint8_t *i2c_meta = (uint8_t *) (i2c_config.meta.vaddr + (meta_alloc*(i2c_config.meta.size / 16)));

    uint8_t flag_mask = I2C_FLAG_READ;
    int ret = i2c_dispatch(&libi2c_conf, (i2c_addr_t)addr, (void *)i2c_meta, (uint16_t)len, flag_mask);
    if (ret) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("i2c_read: libi2c failed to dispatch!"), self->port);
        return -MP_ENOMEM;
    }
    meta_alloc--;
    return len;
}

static int machine_i2c_transfer(mp_obj_base_t *obj, uint16_t addr, size_t n, mp_machine_i2c_buf_t *bufs, unsigned int flags) {
    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(obj);
    // @lesleyr: This implementation currently loses some details when used and is NOT guaranteed
    //           to work if anything else on the system uses I2C! We should implement logic to
    //           build a transfer based on all available MP flags instead of repeatedly creating
    //           smaller ops.

    /* Before doing any transfer operations, we must claim the bus address. */
    /* TODO: we should provide a wrapper API for this like in the timer API */
    microkit_msginfo msginfo = microkit_msginfo_new(I2C_BUS_CLAIM, 1);
    microkit_mr_set(I2C_BUS_SLOT, addr);
    msginfo = microkit_ppcall(i2c_config.virt.id, msginfo);
    seL4_Word claim_label = microkit_msginfo_get_label(msginfo);
    if (claim_label == I2C_FAILURE) {
       mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("I2C(%d): Could not claim bus address %d"), self->port, addr);
        return -MP_EPERM;
    }

    size_t remain_len = 0;
    for (size_t i = 0; i < n; ++i) {
        remain_len += bufs[i].len;
    }

    int num_acks = 0; // only valid for write; for read it'll be 0
    int ret = 0;
    for (; n--; ++bufs) {
        remain_len -= bufs->len;
        if (flags & MP_MACHINE_I2C_FLAG_READ) {
            ret = mp_i2c_read(self, addr, bufs->buf, bufs->len, flags & MP_MACHINE_I2C_FLAG_STOP);
        } else {
            ret = mp_i2c_write(self, addr, bufs->buf, bufs->len);
        }
        if (ret < 0) {
            msginfo = microkit_msginfo_new(I2C_BUS_RELEASE, 1);
            microkit_mr_set(I2C_BUS_SLOT, addr);
            msginfo = microkit_ppcall(i2c_config.virt.id, msginfo);
            seL4_Word release_label = microkit_msginfo_get_label(msginfo);
            assert(release_label == I2C_SUCCESS);
            return ret;
        }
        num_acks += ret;
    }

    msginfo = microkit_msginfo_new(I2C_BUS_RELEASE, 1);
    microkit_mr_set(I2C_BUS_SLOT, addr);
    msginfo = microkit_ppcall(i2c_config.virt.id, msginfo);
    seL4_Word release_label = microkit_msginfo_get_label(msginfo);
    assert(release_label == I2C_SUCCESS);

    return num_acks;
}

mp_obj_t machine_i2c_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    if (!i2c_enabled) {
        mp_raise_msg_varg(&mp_type_NotImplementedError, MP_ERROR_TEXT("MicroPython not configured as sDDF I2C client"));
        return NULL;
    }

    enum { ARG_id, ARG_scl, ARG_sda, ARG_freq, ARG_timeout };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_scl, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_sda, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_freq, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 400000} },
        { MP_QSTR_timeout, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = I2C_DEFAULT_TIMEOUT_US} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_int_t i2c_id = mp_obj_get_int(args[ARG_id].u_obj);

    /* Check that the specified BUS ID is valid */
    int i;
    for (i = 0; i < I2C_AVAILABLE_BUSES; i++) {
        if (i2c_id == permitted_buses[i]) break;
    }
    if (i == I2C_AVAILABLE_BUSES) {
        mp_raise_msg_varg(&mp_type_ValueError,
                          MP_ERROR_TEXT("I2C(%d) doesn't exist or is not supported"), i2c_id);
        return NULL;
    }

    machine_i2c_obj_t *self = &i2c_bus_objs[i2c_id];
    if (self->base.type == NULL) {
        // Created for the first time, set information pins
        self->base.type = &machine_i2c_type;
        self->port = i2c_id;
    }

    return MP_OBJ_FROM_PTR(self);
}

static void machine_i2c_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {

}

static const mp_machine_i2c_p_t machine_i2c_p = {
    .transfer = machine_i2c_transfer
};

MP_DEFINE_CONST_OBJ_TYPE(
    machine_i2c_type,
    MP_QSTR_I2C,
    MP_TYPE_FLAG_NONE,
    make_new, machine_i2c_make_new,
    print, machine_i2c_print,
    protocol, &machine_i2c_p,
    locals_dict, &mp_machine_i2c_locals_dict
);

#endif /* MICROPY_PY_MACHINE_I2C && MICROPY_HW_ENABLE_HW_I2C */
