/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once
#include <microkit.h>
#include <lions/pager/config.h>
#include <lions/posix/pager.h>

void allocator_init(pager_server_config_t *config);

long pager_mem_call(microkit_msginfo msginfo, microkit_child child);
