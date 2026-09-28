/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Input virtualiser.
 *
 * Merges the event streams of all input drivers and delivers them to the
 * active client. Deciding which application should receive input (focus) is
 * the job of the compositor, so the intended configuration is a single
 * client: the compositor, which then routes events to its applications.
 *
 * Channels: driver i is on channel i, client j on INPUT_NUM_DRIVERS + j.
 * Queues: the queues of driver i and client j are mapped at
 * input_driver_queues + i * INPUT_QUEUE_REGION_SIZE and
 * input_client_queues + j * INPUT_QUEUE_REGION_SIZE respectively.
 */

#include <stdint.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <lions/input/input.h>
#include <input_config.h>

#define LOG_VIRT_ERR(...) do{ sddf_dprintf("INPUT VIRT|ERROR: "); sddf_dprintf(__VA_ARGS__); }while(0)

#define CLIENT_CH(i) (INPUT_NUM_DRIVERS + (i))

_Static_assert(INPUT_NUM_DRIVERS + INPUT_NUM_CLIENTS <= MICROKIT_MAX_CHANNELS, "too many input channels");

uintptr_t input_driver_queues;
uintptr_t input_client_queues;

static input_queue_handle_t drivers[INPUT_NUM_DRIVERS];
static input_queue_handle_t clients[INPUT_NUM_CLIENTS];
static uint32_t active_client = 0;

void init(void)
{
    for (int i = 0; i < INPUT_NUM_DRIVERS; i++) {
        input_queue_init(&drivers[i], (input_queue_t *)(input_driver_queues + i * INPUT_QUEUE_REGION_SIZE),
                         INPUT_QUEUE_CAPACITY(INPUT_QUEUE_REGION_SIZE));
    }
    for (int i = 0; i < INPUT_NUM_CLIENTS; i++) {
        input_queue_init(&clients[i], (input_queue_t *)(input_client_queues + i * INPUT_QUEUE_REGION_SIZE),
                         INPUT_QUEUE_CAPACITY(INPUT_QUEUE_REGION_SIZE));
    }
}

void notified(microkit_channel ch)
{
    if (ch >= INPUT_NUM_DRIVERS) {
        LOG_VIRT_ERR("notification on unexpected channel %u\n", ch);
        return;
    }

    /* Drain every driver, not just the one that notified us, to keep ordering fair */
    bool delivered = false;
    input_queue_handle_t *client = &clients[active_client];
    for (int i = 0; i < INPUT_NUM_DRIVERS; i++) {
        input_event_t event;
        while (input_dequeue(&drivers[i], &event) == 0) {
            if (input_enqueue(client, event) == 0) {
                delivered = true;
            }
        }
    }

    if (delivered) {
        microkit_notify(CLIENT_CH(active_client));
    }
}
