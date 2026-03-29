/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <stdint.h>
#include <string.h>
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

// #define DEBUG_MPY_MACHINE_I2C

#ifdef DEBUG_MPY_MACHINE_I2C
    #define debug_printf(...)   mp_printf(&mp_plat_print, "machine_i2c.c: " __VA_ARGS__)
#else
    #define debug_printf(...)
#endif

extern bool i2c_enabled;
extern i2c_client_config_t i2c_config;
extern i2c_queue_handle_t i2c_queue_handle;
extern libi2c_conf_t libi2c_config;

#define I2C_AVAILABLE_BUSES 1
#define I2C_MAX_BUSES 4
mp_int_t permitted_buses[I2C_AVAILABLE_BUSES] = {1};

typedef struct _machine_i2c_obj_t {
    mp_obj_base_t base;
    size_t port;
} machine_i2c_obj_t;

machine_i2c_obj_t i2c_bus_objs[I2C_MAX_BUSES] = {};

#define I2C_DEFAULT_TIMEOUT_US (50000) // 50ms

static i2c_err_t mp_i2c_dispatch(machine_i2c_obj_t *self, uint16_t addr, uint8_t *buf, size_t len, uint8_t flag_mask) {
    if (addr >= (1 << 7)) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("I2C only supports 7-bit addresses."),
                          self->port);
        return -MP_EFAULT;
    }

    // Check length of buffer is sane. libi2c read max is UINT16_MAX.
    // We leave the function header as a size_t to keep micropython happy
    if (len > UINT16_MAX) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("Length is too long. Max = UINT16_MAX"),
                          self->port);
        return -MP_EINVAL;
    }

    // Copy buffer into data region. We assume that nobody else touches the i2c
    // data region in this PD.
    uint8_t *i2c_data = (uint8_t *) i2c_config.data.vaddr;
    memcpy(i2c_data, buf, len);

    // micropython expects the results of the writeread to be written back
    // offset past after the address; sDDF doesn't.
    if (flag_mask & I2C_FLAG_WRRD) {
        len--;
        buf++;
    }

    // Perform read
    int ret = sddf_i2c_nb_dispatch(&libi2c_config, (i2c_addr_t) addr,
                                   i2c_data, (uint16_t) len, flag_mask);

    if (ret != I2C_ERR_OK) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("i2c_read: failed to read!"), self->port);
        return -MP_EFAULT;
    }

    microkit_notify(i2c_config.virt.id);
    mp_cothread_wait(i2c_config.virt.id, MP_WAIT_NO_INTERRUPT);

    /* Now process the response */
    size_t err_cmd_idx = 0;
    i2c_addr_t returned_addr = 0;
    i2c_err_t err = sddf_i2c_nb_return(&libi2c_config, &returned_addr, &err_cmd_idx);
    assert(returned_addr == (i2c_addr_t)addr);

    /* If we were reading, copy out response data */
    if (flag_mask & I2C_FLAG_READ && err == I2C_ERR_OK) {
        memcpy(buf, i2c_data, len);
    }

    return err;
}

// We use i2c_transfer_single because it makes it easier for us to deal with everything
// and when we do the conversion ourselves from multiple buffers to many we need to
// deal with STOP or flag conditions etc in a manner similar to __i2c_dispatch
// So let's not, for simplicity. This entails double copies etc
// (it also makes handling WRRD a little jank, as we need to assume that addrsize == 1)
static int machine_i2c_transfer_single(mp_obj_base_t *obj, uint16_t addr, size_t len, uint8_t *buf, unsigned int flags) {
    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(obj);

    debug_printf("transfer_single: addr=0x%x, buf_len=%ld, buf=%p, flags=0x%x\n", addr, len, buf, flags);

    uint8_t sddf_flags = 0;
    if (flags & MP_MACHINE_I2C_FLAG_STOP) {
        debug_printf("     : flag stop\n");
        flags &= ~MP_MACHINE_I2C_FLAG_STOP;
        sddf_flags |= I2C_FLAG_STOP;
    }
    if (flags & MP_MACHINE_I2C_FLAG_READ) {
        debug_printf("     : flag read\n");
        flags &= ~MP_MACHINE_I2C_FLAG_READ;
        sddf_flags |= I2C_FLAG_READ;
    }
    if (flags & MP_MACHINE_I2C_FLAG_WRITE1) {
        debug_printf("     : write-read\n");
        flags &= ~MP_MACHINE_I2C_FLAG_WRITE1;
        sddf_flags |= I2C_FLAG_WRRD;
    }
    if (flags != 0) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("I2C(%d): unsupported flags 0x%x"), self->port, flags);
        return -MP_EINVAL;
    }

    /* Before doing any transfer operations, we must claim the bus address. */
    if (!i2c_bus_claim(i2c_config.virt.id, addr)) {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                          MP_ERROR_TEXT("I2C(%d): Could not claim bus address %d"), self->port, addr);
        return -MP_EPERM;
    }

    i2c_err_t err = mp_i2c_dispatch(self, addr, buf, len, sddf_flags);

    debug_printf("machine_i2c_transfer: done (err: %d)\n", err);

    // always release the bus regardless of the return (FIXME: not-assert)
    assert(i2c_bus_release(i2c_config.virt.id, addr));

    if (err != I2C_ERR_OK) {
        switch (err) {
        case I2C_ERR_QUEUE:
            return -MP_EFAULT;

        case I2C_ERR_MALFORMED_TRANSACTION:
        case I2C_ERR_MALFORMED_HEADER:
            // Internal error.
            return -MP_EIO;

        case I2C_ERR_UNPERMITTED_ADDR:
            return -MP_EACCES;
        case I2C_ERR_TIMEOUT:
        case I2C_ERR_NACK:
            return -MP_ETIMEDOUT;

        case I2C_ERR_NOREAD:
        case I2C_ERR_BADSEQ:
        case I2C_ERR_OTHER:
        default:
            return -MP_EIO;
        }
    } else {
        return len;
    }
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
    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "I2C(%u, freq=400kHz)", self->port);
}

static const mp_machine_i2c_p_t machine_i2c_p = {
    .transfer_supports_write1 = true,
    .transfer = mp_machine_i2c_transfer_adaptor,
    .transfer_single = machine_i2c_transfer_single
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
