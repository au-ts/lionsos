/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <string.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/config.h>
#include <sddf/util/printf.h>
#include <lions/fs/protocol.h>
#include <lions/fs/config.h>

__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
serial_queue_handle_t tx_queue_handle;

__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;
fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

void init(void)
{
    assert(timer_config_check_magic(&timer_config));
    assert(serial_config_check_magic(&serial_config));
    assert(fs_config_check_magic(&fs_config));

    serial_queue_init(&tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size, serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &tx_queue_handle);

    fs_command_queue = fs_config.server.command_queue.vaddr;
    fs_completion_queue = fs_config.server.completion_queue.vaddr;
    fs_share = fs_config.server.share.vaddr;

    sddf_printf("LionsOS FS benchmark\n");
}

void notified(microkit_channel ch)
{

}