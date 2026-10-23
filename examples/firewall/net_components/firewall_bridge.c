#include <sddf/network/queue.h>
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <sddf/util/cache.h>
#include <lions/firewall/bridge.h>
#include <lions/firewall/config.h>
#include <lions/firewall/ethernet.h>

__attribute__((__section__(".fw_bridge_config"))) fw_bridge_config_t config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;

static net_queue_handle_t rx_ports[FW_BRIDGE_MAX_PORTS]; // Received frames from port
static net_queue_handle_t tx_ports[FW_BRIDGE_MAX_PORTS]; // Transmit frames to port
static net_queue_handle_t local_rx; // Frames for the Rx virtualiser
static net_queue_handle_t local_tx; // Frames from the Tx virtualiser

static fw_fdb_entry_t scratch[FW_FDB_NUM_ENTRIES];
static fw_fdb_entry_t entries[FW_FDB_NUM_ENTRIES];

static uint64_t ages_to_clean = FDB_AGES_PER_CLEAN;

typedef struct buffer_location {
    uint8_t region;
    size_t slot;
    uintptr_t vaddr;
} buffer_location_t;

enum buffer_origin {
    BUFFER_IDLE = 0, // Not actively forwarding (does not mean buffer is free/unowned)
    BUFFER_PHYSICAL_RX,
    BUFFER_LOCAL_TX,
};

typedef struct bridge_buffer_state {
    // First descriptor received for forwarding
    net_buff_desc_t original;
    // Where did this buffer come from?
    enum buffer_origin origin;
    // Are we waiting on sending this buffer on a port?
    uint16_t tx_pending;
    // Do we need to send this to Rx virtualiser when done?
    bool deliver_local;
} bridge_buffer_state_t;

// Constant from "dma_buffer_queue" in constants.py
#define BRIDGE_MAX_BUFFERS_PER_REGION 512

static bridge_buffer_state_t buffer_states[FW_MAX_INTERFACES + SDDF_NET_MAX_CLIENTS][BRIDGE_MAX_BUFFERS_PER_REGION];
static uint32_t tx_outstanding[FW_BRIDGE_MAX_PORTS];

static bool get_buffer_location(const net_buff_desc_t *buffer, buffer_location_t *location)
{
    if (buffer->len > NET_BUFFER_SIZE) {
        return false;
    }

    for (uint8_t i = 0; i < config.num_data_regions; i++) {
        const device_region_resource_t *region = &config.data_regions[i];

        if (buffer->io_or_offset < region->io_addr) {
            continue;
        }

        uint64_t offset = buffer->io_or_offset - region->io_addr;
        if (offset >= region->region.size) {
            continue;
        }

        // Offset should be aligned and allow enough space for a full buffer
        if (offset % NET_BUFFER_SIZE != 0 || region->region.size - offset < NET_BUFFER_SIZE) {
            return false;
        }

        location->region = i;
        location->slot = offset / NET_BUFFER_SIZE;
        location->vaddr = (uintptr_t)region->region.vaddr + offset;
        return true;
    }

    return false;
}

static bridge_buffer_state_t *get_buffer_state(const buffer_location_t *location)
{
    assert(location->region < config.num_data_regions);
    assert(location->slot < BRIDGE_MAX_BUFFERS_PER_REGION);

    return &buffer_states[location->region][location->slot];
}

typedef struct bridge_rx_destinations {
    bool ports[FW_BRIDGE_MAX_PORTS];
    bool deliver_local;
    uint16_t tx_len;
} bridge_rx_destinations_t;

static bool is_multicast(const uint8_t mac[MAC802_BYTES])
{
    return mac[0] & 1;
}

static bool is_bridge_control(const uint8_t mac[MAC802_BYTES])
{
    const uint8_t prefix[MAC802_BYTES - 1] = { 0x01, 0x80, 0xc2, 0x00, 0x00 };
    return memcmp(mac, prefix, sizeof(prefix)) == 0 && mac[5] <= 0x0f;
}

// Take packets from drivers before they are sent to the Rx virtualiser and work out
// where they should be sent. Returns false if the frame should be dropped.
// Does not do any forwarding or modify the buffer in any way.
static bool rx_select_destinations(uint8_t ingress_port, const net_buff_desc_t *buffer,
                                   bridge_rx_destinations_t *destinations)
{
    *destinations = (bridge_rx_destinations_t) { 0 };
    assert(ingress_port < config.num_ports);

    buffer_location_t location;
    if (!get_buffer_location(buffer, &location) || location.region != 0 || location.slot >= local_rx.capacity) {
        return false;
    }

    uint16_t frame_len = buffer->len;
// TODO: I think I need this but should it be here and Rx virt?
#if !defined(CONFIG_PLAT_QEMU_ARM_VIRT)
    // Match Rx virt's trailer handling, but keep the original length for local delivery.
    if (frame_len < 4) {
        return false;
    }
    frame_len -= 4;
#endif
    if (frame_len < ETH_HDR_LEN) {
        return false;
    }

    // TODO: Copied from Rx virt, do we remove the other one?
    // Cache invalidate after DMA write, so we don't read stale data.
    // This must be performed after the DMA write to avoid reading
    // data that was speculatively fetched before the DMA write.
    //
    // We would invalidate if it worked in usermode. Alas, it
    // does not -- see [1]. The fastest operation that works is a
    // usermode CleanInvalidate (faster than a Invalidate via syscall).
    //
    // [1]: https://developer.arm.com/documentation/ddi0595/2021-06/AArch64-Instructions/DC-IVAC--Data-or-unified-Cache-line-Invalidate-by-VA-to-PoC
    cache_clean_and_invalidate(location.vaddr, location.vaddr + buffer->len);

    const eth_hdr_t *eth = (const eth_hdr_t *)location.vaddr;
    const uint8_t zero_mac[MAC802_BYTES] = { 0 };

    // Drop frames with an invalid source address (multicast, all zero, or our own MAC)
    if (is_multicast(eth->ethsrc_addr) || memcmp(eth->ethsrc_addr, zero_mac, MAC802_BYTES) == 0
        || memcmp(eth->ethsrc_addr, config.mac_addr, MAC802_BYTES) == 0) {
        return false;
    }

    // Drop reserved bridge-control destinations (01:80:c2:00:00:00 through 0f)
    // (Bridge control frames are not implemented in the firewall)
    if (is_bridge_control(eth->ethdst_addr)) {
        return false;
    }

    // No need to check learn succeeded here (we can still send, just less efficient)
    fw_fdb_learn(entries, eth->ethsrc_addr, ingress_port);
    destinations->tx_len = frame_len;

    // Check if the destination is our own mac address, no need to forward, just send to the Rx virtualiser
    if (memcmp(eth->ethdst_addr, config.mac_addr, MAC802_BYTES) == 0) {
        destinations->deliver_local = true;
        return true;
    }

    if (is_multicast(eth->ethdst_addr)) {
        // Broadcast and multicast are delivered to interface as well as flooded on all ports
        destinations->deliver_local = true;
    } else {
        // This is unicast traffic not to the interface, so try to find a port
        uint8_t port;
        if (fw_fdb_lookup(entries, eth->ethdst_addr, &port)) {
            // We succeeded to only send there
            assert(port < config.num_ports);
            if (port != ingress_port) {
                destinations->ports[port] = true;
            }
            return true;
        }
        // We don't know, so this is the same as flooding (minus ingress)
    }

    // Flood group destinations and unknown unicast (minus ingress)
    for (uint8_t port = 0; port < config.num_ports; port++) {
        destinations->ports[port] = port != ingress_port;
    }

    return true;
}

static void tx_select_destinations(const net_buff_desc_t *buffer, const buffer_location_t *location,
                                   bool ports[FW_BRIDGE_MAX_PORTS])
{
    if (buffer->len < ETH_HDR_LEN || buffer->len > NET_BUFFER_SIZE) {
        return;
    }

    const eth_hdr_t *eth = (const eth_hdr_t *)location->vaddr;

    if (is_bridge_control(eth->ethdst_addr) || memcmp(eth->ethdst_addr, config.mac_addr, MAC802_BYTES) == 0) {
        return;
    }

    if (!is_multicast(eth->ethdst_addr)) {
        uint8_t port;
        if (fw_fdb_lookup(entries, eth->ethdst_addr, &port)) {
            assert(port < config.num_ports);
            ports[port] = true;
            return;
        }
    }

    // If we don't know the destination or it's a multicast, we flood to all ports
    for (uint8_t port = 0; port < config.num_ports; port++) {
        ports[port] = true;
    }
}

static bool tx_enqueue(uint8_t port, net_buff_desc_t buffer, bridge_buffer_state_t *state)
{
    // Check outstanding rather than active queue so that we don't accdidentally
    // enqueue more active before we pull completed buffers back from the driver.
    if (tx_outstanding[port] >= tx_ports[port].capacity) {
        return false;
    }

    state->tx_pending++;
    tx_outstanding[port]++;
    int err = net_enqueue_active(&tx_ports[port], buffer);
    if (err) {
        state->tx_pending--;
        tx_outstanding[port]--;
        return false;
    }

    return true;
}

static void tx_notify(const bool notify_ports[FW_BRIDGE_MAX_PORTS])
{
    for (uint8_t port = 0; port < config.num_ports; port++) {
        if (notify_ports[port] && net_require_signal_active(&tx_ports[port])) {
            net_cancel_signal_active(&tx_ports[port]);
            microkit_notify(config.ports[port].tx.id);
        }
    }
}

// Decide where a buffer is returned to once done forwarding to physical ports
static void finish_buffer(bridge_buffer_state_t *state)
{
    // Buffer should be in forwarding state
    assert(state->origin != BUFFER_IDLE);
    // No more physical ports should need this buffer
    assert(state->tx_pending == 0);

    net_buff_desc_t buffer = state->original;
    enum buffer_origin origin = state->origin;
    bool deliver_local = state->deliver_local;

    // Mark that forwarding is done (buffer may not be free yet though)
    state->origin = BUFFER_IDLE;
    state->deliver_local = false;

    if (origin == BUFFER_LOCAL_TX) {
        // Buffer came from Tx virtualiser, so can be returned there
        int err = net_enqueue_free(&local_tx, buffer);
        assert(!err);
        if (net_require_signal_free(&local_tx)) {
            net_cancel_signal_free(&local_tx);
            microkit_notify(config.local_tx.id);
        }
    } else if (deliver_local) {
        // Buffer is now safe to send to Rx virtualiser (it may be written to)
        int err = net_enqueue_active(&local_rx, buffer);
        assert(!err);
        if (net_require_signal_active(&local_rx)) {
            net_cancel_signal_active(&local_rx);
            microkit_notify(config.local_rx.id);
        }
    } else {
        // Buffer came from driver Rx and is not needed
        size_t slot = (buffer.io_or_offset - config.data_regions[0].io_addr) / NET_BUFFER_SIZE;
        size_t port = slot % config.num_ports;
        buffer.len = 0;

        int err = net_enqueue_free(&rx_ports[port], buffer);
        assert(!err);
        if (net_require_signal_free(&rx_ports[port])) {
            net_cancel_signal_free(&rx_ports[port]);
            microkit_notify(config.ports[port].rx.id);
        }
    }
}

static void tx_return(void)
{
    // Loop over all ports and return any buffers that the driver is done transmitting
    for (uint8_t port = 0; port < config.num_ports; port++) {
        while (true) {
            while (!net_queue_empty_free(&tx_ports[port])) {
                net_buff_desc_t buffer;
                int err = net_dequeue_free(&tx_ports[port], &buffer);
                assert(!err);

                // Check that this buffer is correctly in a forwarding state
                buffer_location_t location;
                bool valid = get_buffer_location(&buffer, &location);
                assert(valid);
                bridge_buffer_state_t *state = get_buffer_state(&location);
                assert(state->origin != BUFFER_IDLE);
                assert(state->tx_pending > 0);
                assert(tx_outstanding[port] > 0);

                // Mark transmission as done and maybe return the buffer if no more ports need it
                state->tx_pending--;
                tx_outstanding[port]--;
                if (state->tx_pending == 0) {
                    finish_buffer(state);
                }
            }

            net_request_signal_free(&tx_ports[port]);
            if (net_queue_empty_free(&tx_ports[port])) {
                break;
            }
            net_cancel_signal_free(&tx_ports[port]);
        }
    }
}

static void tx_provide(void)
{
    // Handle buffers from the Tx virtualiser and forward them to physical ports
    bool notify_ports[FW_BRIDGE_MAX_PORTS] = { false };

    while (true) {
        while (!net_queue_empty_active(&local_tx)) {
            net_buff_desc_t buffer;
            int err = net_dequeue_active(&local_tx, &buffer);
            assert(!err);

            net_buff_desc_t identity = buffer;
            // Use length 0 here so even if we have malformed buffer (over max length)
            // we can still find it and recycle it
            // TODO: Is this sane?
            identity.len = 0;
            buffer_location_t location;
            bool valid = get_buffer_location(&identity, &location);
            assert(valid);

            bridge_buffer_state_t *state = get_buffer_state(&location);
            assert(state->origin == BUFFER_IDLE && state->tx_pending == 0);
            *state = (bridge_buffer_state_t) {
                .original = buffer,
                .origin = BUFFER_LOCAL_TX,
            };

            bool ports[FW_BRIDGE_MAX_PORTS] = { false };
            tx_select_destinations(&buffer, &location, ports);
            for (uint8_t port = 0; port < config.num_ports; port++) {
                if (ports[port] && tx_enqueue(port, buffer, state)) {
                    notify_ports[port] = true;
                }
            }

            // If we dropped the buffer (no ports/space) we should just return to sender now
            if (state->tx_pending == 0) {
                finish_buffer(state);
            }
        }

        net_request_signal_active(&local_tx);
        if (net_queue_empty_active(&local_tx)) {
            break;
        }
        net_cancel_signal_active(&local_tx);
    }

    tx_notify(notify_ports);
}

static void rx_return(void)
{
    // Handle received buffers (Rx) from drivers and start forwarding them
    bool notify_ports[FW_BRIDGE_MAX_PORTS] = { false };

    for (uint8_t ingress = 0; ingress < config.num_ports; ingress++) {
        while (true) {
            while (!net_queue_empty_active(&rx_ports[ingress])) {
                net_buff_desc_t buffer;
                int err = net_dequeue_active(&rx_ports[ingress], &buffer);
                assert(!err);

                // Locate the slot
                net_buff_desc_t identity = buffer;
                // Use length 0 here so even if we have malformed buffer (over max length)
                // we can still find it and recycle it
                // TODO: Is this sane?
                identity.len = 0;
                buffer_location_t location;
                bool valid = get_buffer_location(&identity, &location);
                assert(valid);
                // We expect that this is in the interface's Rx pool
                assert(location.region == 0 && location.slot < local_rx.capacity);
                // Extra sanity check here that the slot was assigned to this port
                assert(location.slot % config.num_ports == ingress);

                bridge_buffer_state_t *state = get_buffer_state(&location);
                assert(state->origin == BUFFER_IDLE && state->tx_pending == 0);
                *state = (bridge_buffer_state_t) {
                    .original = buffer,
                    .origin = BUFFER_PHYSICAL_RX,
                };

                bridge_rx_destinations_t destinations;
                if (rx_select_destinations(ingress, &buffer, &destinations)) {
                    state->deliver_local = destinations.deliver_local;
                    net_buff_desc_t outgoing = buffer;
                    outgoing.len = destinations.tx_len;

                    for (uint8_t port = 0; port < config.num_ports; port++) {
                        if (destinations.ports[port] && tx_enqueue(port, outgoing, state)) {
                            notify_ports[port] = true;
                        }
                    }
                }

                // Didn't go to any ports, so we can return it to the driver or Rx virtualiser
                if (state->tx_pending == 0) {
                    finish_buffer(state);
                }
            }

            net_request_signal_active(&rx_ports[ingress]);
            if (net_queue_empty_active(&rx_ports[ingress])) {
                break;
            }
            net_cancel_signal_active(&rx_ports[ingress]);
        }
    }

    tx_notify(notify_ports);
}

static void rx_provide(void)
{
    bool notify_ports[FW_BRIDGE_MAX_PORTS] = { false };

    while (true) {
        while (!net_queue_empty_free(&local_rx)) {
            net_buff_desc_t buffer;
            int err = net_dequeue_free(&local_rx, &buffer);
            assert(!err);

            buffer_location_t location;
            bool valid = get_buffer_location(&buffer, &location);
            assert(valid);

            // Region 0 is configured as this interface's Rx pool
            assert(location.region == 0);
            assert(location.slot < local_rx.capacity);

            bridge_buffer_state_t *state = get_buffer_state(&location);
            assert(state->origin == BUFFER_IDLE);
            assert(state->tx_pending == 0);

            // TODO: Is this sane?
            // Cache clean since Rx virt or further PDs may have written to the buffer
            cache_clean_and_invalidate(location.vaddr, location.vaddr + NET_BUFFER_SIZE);

            // Simples method for assigning buffers to ports is just divide evenly
            // Could do this smarter in the future?
            size_t port = location.slot % config.num_ports;
            buffer.len = 0;

            err = net_enqueue_free(&rx_ports[port], buffer);
            assert(!err);
            notify_ports[port] = true;
        }

        // Ask for a signal when new buffer is available
        net_request_signal_free(&local_rx);

        // None are available so stop processing
        if (net_queue_empty_free(&local_rx)) {
            break;
        }

        // More handling to do, so cancel request and keep going
        net_cancel_signal_free(&local_rx);
    }

    for (size_t port = 0; port < config.num_ports; port++) {
        if (notify_ports[port] && net_require_signal_free(&rx_ports[port])) {
            net_cancel_signal_free(&rx_ports[port]);
            microkit_notify(config.ports[port].rx.id);
        }
    }
}

void notified(microkit_channel ch)
{
    if (ch == timer_config.driver_id) {
        fw_fdb_age(entries);

        ages_to_clean--;
        if (ages_to_clean == 0) {
            fw_fdb_clean(entries, scratch);
            ages_to_clean = FDB_AGES_PER_CLEAN;
        }

        sddf_timer_set_timeout(timer_config.driver_id, FDB_AGEING_PERIOD_NS);
    }

    tx_return();
    rx_provide();
    rx_return();
    tx_provide();
}

void init(void)
{
    assert(timer_config_check_magic(&timer_config));
    sddf_timer_set_timeout(timer_config.driver_id, FDB_AGEING_PERIOD_NS);

    assert(config.num_ports > 0 && config.num_ports <= FW_BRIDGE_MAX_PORTS);
    // At least this interface's Rx pool must be configured
    assert(config.num_data_regions > 0 && config.num_data_regions <= FW_MAX_INTERFACES + SDDF_NET_MAX_CLIENTS);
    // There should be at least one buffer per port
    assert(config.local_rx.num_buffers >= config.num_ports);
    // Ensure buffers fit inside the configured region
    assert(config.local_rx.num_buffers <= config.data_regions[0].region.size / NET_BUFFER_SIZE);

    // Ensure bookkeeping has room for every buffer in each mapped pool
    for (uint8_t i = 0; i < config.num_data_regions; i++) {
        size_t num_buffers = config.data_regions[i].region.size / NET_BUFFER_SIZE;
        assert(num_buffers > 0);
        assert(num_buffers <= BRIDGE_MAX_BUFFERS_PER_REGION);
    }

    // Configure active and free queues for Tx/Rx virtualisers
    net_queue_init(&local_rx, config.local_rx.free_queue.vaddr, config.local_rx.active_queue.vaddr,
                   config.local_rx.num_buffers);
    net_queue_init(&local_tx, config.local_tx.free_queue.vaddr, config.local_tx.active_queue.vaddr,
                   config.local_tx.num_buffers);

    // Configure active and free queues for each port
    for (uint8_t i = 0; i < config.num_ports; i++) {
        net_queue_init(&rx_ports[i], config.ports[i].rx.free_queue.vaddr, config.ports[i].rx.active_queue.vaddr,
                       config.ports[i].rx.num_buffers);
        net_queue_init(&tx_ports[i], config.ports[i].tx.free_queue.vaddr, config.ports[i].tx.active_queue.vaddr,
                       config.ports[i].tx.num_buffers);
        tx_outstanding[i] = 0;

        // Ensure each port has enough buffers for its portion of the interface's Rx pool
        size_t assigned = local_rx.capacity / config.num_ports;
        if (i < local_rx.capacity % config.num_ports) {
            assigned++;
        }
        assert(rx_ports[i].capacity >= assigned);
    }

    fw_fdb_init(entries);
    tx_return();
    rx_provide();
    rx_return();
    tx_provide();
}
