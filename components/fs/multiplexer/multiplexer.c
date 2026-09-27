/*
 * SPDX-FileCopyrightText: 2026 UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <stdbool.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include <lions/fs/multiplexer.h>
#include <lions/fs/config.h>

__attribute__((section(".fs_multiplexer_config")))
fs_multiplexer_config_t fs_multiplexer_config;

static fs_queue_t *server_commands;
static fs_queue_t *server_completions;
/* Completion slots reserved by requests forwarded to the server. */
static uint64_t reserved[FS_MULTIPLEXER_MAX_CLIENTS];
/* Failed clients no longer receive requests or completions. */
static bool client_failed[FS_MULTIPLEXER_MAX_CLIENTS];

static int client_from_channel(microkit_channel ch)
{
    for (uint64_t i = 0; i < fs_multiplexer_config.num_clients; i++) {
        if (fs_multiplexer_config.clients[i].id == ch) {
            return (int)i;
        }
    }
    return -1;
}

void init(void)
{
    assert(fs_config_check_magic(&fs_multiplexer_config));
    assert(fs_multiplexer_config.num_clients > 0);
    assert(fs_multiplexer_config.num_clients <= FS_MULTIPLEXER_MAX_CLIENTS);
    server_commands = fs_multiplexer_config.server.command_queue.vaddr;
    server_completions = fs_multiplexer_config.server.completion_queue.vaddr;
}

static void forward_client_requests(uint64_t client_id)
{
    /* Make sure the request is from a valid client */
    if (unlikely(client_failed[client_id])) {
        return;
    }

    fs_connection_resource_t *client = &fs_multiplexer_config.clients[client_id];
    fs_queue_t *commands = client->command_queue.vaddr;
    fs_queue_t *completions = client->completion_queue.vaddr;
    uint64_t count = fs_queue_length_consumer(commands);

    // @gt ?? what else if a client's queue being messed up?
    if (unlikely(count > FS_QUEUE_CAPACITY)) {
        client_failed[client_id] = true;
        return;
    }

    /* Check whether the server has slots for receiving requests */
    uint64_t server_available = FS_QUEUE_CAPACITY - fs_queue_length_producer(server_commands);
    if (count > server_available) {
        /* Make sure the server has capacity for request */
        count = server_available;
    }

    /* Check whether the client has slots for receiving completion events */
    uint64_t completion_used = fs_queue_length_producer(completions);
    if (unlikely(completion_used > FS_QUEUE_CAPACITY)) {
        client_failed[client_id] = true;
        return;
    }
    uint64_t completion_available = FS_QUEUE_CAPACITY - completion_used;
    // Slots reserved for outstanding requests are over-provisioned, which is wrong...
    if (unlikely(reserved[client_id] > completion_available)) {
        client_failed[client_id] = true;
        return;
    } else if (reserved[client_id] == completion_available) {
        return;
    }

    completion_available -= reserved[client_id];
    if (count > completion_available) {
        /* Make sure the client has capacity for completion */
        count = completion_available;
    }

    for (uint64_t i = 0; i < count; i++) {
        fs_cmd_t command = fs_queue_idx_filled(commands, i)->cmd;
        if (unlikely(command.id > FS_MUX_REQUEST_ID_MASK)) {
            client_failed[client_id] = true;
            return;
        }
        command.id = fs_multiplexer_encode_id(client_id, command.id);
        fs_queue_idx_empty(server_commands, i)->cmd = command;
    }
    if (count) {
        /* Reserve space for every completion before exposing the requests. */
        reserved[client_id] += count;
        fs_queue_publish_consumption(commands, count);
        fs_queue_publish_production(server_commands, count);
        microkit_notify(fs_multiplexer_config.server.id);
    }
}

static bool should_discard_completion(uint64_t client_id, uint64_t produced,
                                      uint64_t available)
{
    if (client_failed[client_id]) {
        // Simply, skip the completion events for failed clients
        reserved[client_id]--;
        return true;
    }
    /* Invariant: each event in "produced" has a reserved slot in completion queue */
    // @gt ?? this branch never happens unless someone breaks the invariant
    //        by corrupting the completion queue metadata or reservation accounting
    if (unlikely(produced >= available)) {
        client_failed[client_id] = true;
        reserved[client_id]--;
        return true;
    }
    return false;
}

static void forward_server_completions(void)
{
    /* Free completion slots observed for each client. */
    uint64_t available[FS_MULTIPLEXER_MAX_CLIENTS];
    /* Completions staged for each client but not yet published. Incrementing
     * produced[i] decrements reserved[i], so their sum remains bounded by
     * available[i] for every healthy client. */
    uint64_t produced[FS_MULTIPLEXER_MAX_CLIENTS] = {0};

    for (uint64_t i = 0; i < fs_multiplexer_config.num_clients; i++) {
        fs_queue_t *queue = fs_multiplexer_config.clients[i].completion_queue.vaddr;
        uint64_t used = fs_queue_length_producer(queue);
        if (unlikely(used > FS_QUEUE_CAPACITY)) {
            client_failed[i] = true;
            available[i] = 0;
        } else {
            available[i] = FS_QUEUE_CAPACITY - used;
        }
    }
    // @gt ?? invariant: reserved[i] + produced[i] <= available[i]

    uint64_t consumed = 0;
    uint64_t count = fs_queue_length_consumer(server_completions);
    while (consumed < count) {
        fs_cmpl_t completion = fs_queue_idx_filled(server_completions, consumed)->cmpl;
        uint64_t client_id = fs_multiplexer_client_id(completion.id);
        assert(client_id < fs_multiplexer_config.num_clients);
        assert(reserved[client_id] > 0);
        if (should_discard_completion(client_id, produced[client_id], available[client_id])) {
            consumed++;
            continue;
        }
        fs_queue_t *queue = fs_multiplexer_config.clients[client_id].completion_queue.vaddr;
        completion.id = fs_multiplexer_request_id(completion.id);
        fs_queue_idx_empty(queue, produced[client_id]++)->cmpl = completion;
        /* The reserved slot is now occupied by the delivered completion. */
        reserved[client_id]--;
        consumed++;
    }
    if (consumed) fs_queue_publish_consumption(server_completions, consumed);
    for (uint64_t i = 0; i < fs_multiplexer_config.num_clients; i++) {
        if (produced[i]) {
            fs_queue_t *queue = fs_multiplexer_config.clients[i].completion_queue.vaddr;
            fs_queue_publish_production(queue, produced[i]);
            // @gt ?? Do not contact failed clients again?
            if (unlikely(client_failed[i])) {
                continue;
            }
            microkit_notify(fs_multiplexer_config.clients[i].id);
        }
        /* A completion frees a server slot; retry clients held by backpressure. */
        forward_client_requests(i);
    }
}

void notified(microkit_channel ch)
{
    if (ch == fs_multiplexer_config.server.id) {
        forward_server_completions();
        return;
    }
    int client_id = client_from_channel(ch);
    if (client_id >= 0) forward_client_requests((uint64_t)client_id);
}
