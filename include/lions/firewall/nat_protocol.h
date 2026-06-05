/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

/* PP call parameters for webserver to enable/disable/query NAT on a virtualizer */
typedef enum fw_nat_pp_type {
    NAT_SET_ENABLED = 0,
    NAT_GET_ENABLED,
} fw_nat_pp_type_t;

/* Argument indices for NAT_SET_ENABLED */
typedef enum {
    NAT_SET_ENABLED_ARG_ENABLED = 0, /* bool: 1 = enable, 0 = disable */
    NAT_SET_ENABLED_NUM_ARGS,
} fw_nat_set_enabled_args_t;

/* Return value indices */
typedef enum {
    NAT_RET_ERR = 0,
    NAT_RET_ENABLED = 1, /* used by NAT_GET_ENABLED */
} fw_nat_ret_args_t;

/* Error codes */
typedef enum {
    NAT_ERR_OKAY = 0,
    NAT_ERR_FAILURE,
} fw_nat_err_t;
