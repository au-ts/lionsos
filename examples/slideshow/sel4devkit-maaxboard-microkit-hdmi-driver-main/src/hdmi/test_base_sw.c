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
 * test_base_sw.c
 *
 ******************************************************************************
 */

#include <microkit.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "write_register.h"


uintptr_t hdmi_base;

int cdn_apb_read(unsigned int addr, unsigned int *value)
{
	*value  = *((uint32_t*)(hdmi_base + addr));
	return 0;
}

int cdn_apb_write(unsigned int addr, unsigned int value)
{
	write_uint_to_mem((unsigned int*)((hdmi_base + addr)), value);
	return 0;
}

int cdn_sapb_read(unsigned int addr, unsigned int *value)
{
	return 0;
}

int cdn_sapb_write(unsigned int addr, unsigned int value)
{
	return 0;
}

void cdn_sleep(uint32_t ms)
{
}

void cdn_usleep(uint32_t us)
{
}


