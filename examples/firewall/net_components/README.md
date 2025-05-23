# Firewall Network Components

These components are based on the standard sDDF network components. The tx
virtualiser for the firewall remains the same, with modifications only being made
to the rx virtualiser.

## Rx Virtualiser

This is based on the original sDDF component that multiplexes based on the MAC
address of the packet, and routes them to the corresponding component. However,
for this project, we want to multiplex based on the protocol of the packet and
forward them to the corresponding filtering component. *We currently only support
IPv4 packets.* Here is how the filters and virtualiser are connected:

![](../images/firewall_net_components.svg)

The `microkit_sdf_gen` tool emits a new firewall config struct:

```c
typedef struct firewall_net_virt_rx_config {
    uint16_t active_client_protocols[SDDF_NET_MAX_CLIENTS];
    firewall_connection_resource_t free_clients[FIREWALL_MAX_FIREWALL_CLIENTS];
    uint8_t num_free_clients;
} firewall_net_virt_rx_config_t;
```

The `active_client_protocols` is the IPv4 protocol number associated
with each filter, and has a one-to-one mapping with the `clients` array
in the existing `net_virt_rx_config_t` struct.