/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <string.h>
#include <stdbool.h>
#include <sddf/benchmark/bench.h>
#include <sddf/benchmark/config.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/config.h>
#include <sddf/util/printf.h>
#include <lions/fs/protocol.h>
#include <lions/fs/config.h>

__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
serial_queue_handle_t tx_queue_handle;
serial_queue_handle_t rx_queue_handle;

__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;
fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

__attribute__((__section__(".benchmark_client_config"))) benchmark_client_config_t benchmark_config;

#define CMD_BENCH_START 'b'
#define CMD_BENCH_STOP 'c'

struct bench *bench;

microkit_channel bench_start_ch;
microkit_channel bench_stop_ch;

uint64_t start;
uint64_t idle_ccount_start;

bool bench_in_progress = false;

void init(void)
{
    assert(timer_config_check_magic(&timer_config));
    assert(serial_config_check_magic(&serial_config));
    assert(fs_config_check_magic(&fs_config));

    serial_queue_init(&tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size, serial_config.tx.data.vaddr);
    serial_queue_init(&rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size, serial_config.rx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &tx_queue_handle);

    fs_command_queue = fs_config.server.command_queue.vaddr;
    fs_completion_queue = fs_config.server.completion_queue.vaddr;
    fs_share = fs_config.server.share.vaddr;

    bench = benchmark_config.cycle_counters;
    bench_start_ch = benchmark_config.start_ch;
    bench_stop_ch = benchmark_config.stop_ch;

    sddf_printf("LionsOS FS benchmark: press 'b' to start a bench run\n");
}

void print_cpu_util(uint64_t total, uint64_t idle) {
    sddf_printf("LionsOS FS benchmark: total cycle %lu, idle cycle %lu, CPU util %.3lf\n", total, idle, ((double) (total - idle) / (double) total) * 100);
}

void process_cmd(char c) {
    switch (c) {
    case CMD_BENCH_START:
        sddf_printf("LionsOS FS benchmark: benchmark start command received!\n");

        if (bench_in_progress) {
            sddf_printf("LionsOS FS benchmark: ERROR: a benchmark run is already in progress. Avoid sending input during a benchmark run.\n");
            break;
        } else {
            bench_in_progress = true;
        }

        start = __atomic_load_n(&bench->ts, __ATOMIC_RELAXED);
        idle_ccount_start = __atomic_load_n(&bench->ccount, __ATOMIC_RELAXED);
        microkit_notify(bench_start_ch);

        break;
    case CMD_BENCH_STOP:
        sddf_printf("LionsOS FS benchmark: benchmark stop command received!\n");

        if (!bench_in_progress) {
            sddf_printf("LionsOS FS benchmark: ERROR: no benchmark is currently running.\n");
            break;
        }

        uint64_t total = __atomic_load_n(&bench->ts, __ATOMIC_RELAXED) - start;
        uint64_t idle = __atomic_load_n(&bench->ccount, __ATOMIC_RELAXED) - idle_ccount_start;
        microkit_notify(bench_stop_ch);
        bench_in_progress = false;

        sddf_printf("LionsOS FS benchmark have been interrupted\n");
        print_cpu_util(total, idle);

        break;
    default:
        sddf_printf("LionsOS FS benchmark: unknown command '%c' received!\n", c);
        break;
    }
}

void notified(microkit_channel ch)
{
    if (ch == serial_config.rx.id) {
        char c;
        while (!serial_dequeue(&rx_queue_handle, &c)) {
            if (c == '\r' || c == '\n') {
                sddf_putchar_unbuffered(c);
            } else {
                process_cmd(c);
            } 
        }
    }
}