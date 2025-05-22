# Routing Components

This repository contains two routing component, `routing_external.c` which routes packets from the
external network to the internal network, and `routing_internal.c` which does the opposite.

Both of these components receive packets from filtering components and transmit out of the opposite NIC.
These routers contain routing tables which the user can configure from the webserver component, and will
make the best possible match for the given destination IP.

They are also connected to the ARP requesters. When a route has been found, the router will consult
the ARP tables to see if there is a valid existing mapping of IP <-> MAC. If not, the router
will enqueue a request to the ARP requester, and place the packet into a waiting queue. This waiting queue
is a linked list of packets and their corresponding IP addresses. For packets with the same desitnation IP,
they are put into a child linked list from the root node. On a successful ARP response will use the supplied MAC
to send the packet out. On an unsuccessful response, the router will drop the packet.

## External Router
Here, the filters are connected to the rx virtualiser of the external network, and connected to the tx virtualiser
of the internal network.

![](../images/external_router.svg)

## Internal Router

The main difference between the external and internal router is that the internal also forwards certain packets
that are addressed to the firewall to internal components. For instance, a packet addressed to the firewall
and on port 80 will be forwarded to the webserver component.

![](../images/internal_router.svg)
