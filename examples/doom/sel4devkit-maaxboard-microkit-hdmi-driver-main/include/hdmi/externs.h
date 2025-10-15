/******************************************************************************
 *
 * Copyright (C) 2016-2017 Cadence Design Systems, Inc.
 * All rights reserved worldwide.
 *
 * Copyright 2017-2018 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 ******************************************************************************
 *
 * externs.h
 *
 ******************************************************************************
 */

#ifndef EXTERNS_H_
#define EXTERNS_H_

#ifndef __UBOOT__
#include <stdint.h>

#else
#include <common.h>
#endif

#include "uboot_conversions.h"

/**
 * \addtogroup UTILS
 * \{
 */
/**
 * \brief read from apb
 * \param addr - address to read
 * \param value - pointer to store value
 * \return non-zero value if error
 */
/*extern int cdn_bus_read(unsigned int addr, unsigned int* value);*/

/**
 * \brief write to apb
 * \param addr - address to write
 * \param value - value to write
 * \return non-zero if error
 */
/*extern int cdn_bus_write(unsigned int addr, unsigned int value);*/

u32 cdn_apb_read(u32 addr, u32 *value);
u32 cdn_sapb_read(u32 addr, u32 *value);
u32 cdn_apb_write(u32 addr, u32 value);
u32 cdn_sapb_write(u32 addr, u32 value);
// u32 hdp_rx_apb_read(u32 addr, u32 *value);
// u32 hdp_rx_sapb_read(u32 addr, u32 *value);
// u32 hdp_rx_apb_write(u32 addr, u32 value);
// u32 hdp_rx_sapb_write(u32 addr, u32 value);
#endif
