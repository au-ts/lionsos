#include <microkit.h>

#include "extmod/machine_i2c.h"
#include "modmachine.h"
#include "py/runtime.h"


#define I2C_AVAILABLE_BUSES 1
#define I2C_MAX_BUSES 4
mp_int_t permitted_buses[I2C_AVAILABLE_BUSES] = {1};

typedef struct _machine_i2c_obj_t {
    mp_obj_base_t base;
    size_t port;
} machine_i2c_obj_t;

machine_i2c_obj_t i2c_bus_objs[I2C_MAX_BUSES] = {};

#define I2C_DEFAULT_TIMEOUT_US (50000) // 50ms

STATIC int machine_i2c_transfer_single(mp_obj_base_t *self_in, uint16_t addr, size_t len, uint8_t *buf, unsigned int flags) {
    return 0;
}

mp_obj_t machine_i2c_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {

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

STATIC void machine_i2c_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {

}

STATIC const mp_machine_i2c_p_t machine_i2c_p = {
    .transfer = mp_machine_i2c_transfer_adaptor,
    .transfer_single = machine_i2c_transfer_single,
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

