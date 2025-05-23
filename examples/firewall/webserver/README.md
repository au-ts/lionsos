# Webserver

We provide a basic GUI that can be accessed by any device connected on the internal network, and accessing
the IP address of the firewall on port 80. This GUI allows you to view and edit all the filtering rules
and routes for both the internal and external networks.

The webserver runs on top of Micropython using the Microdot library. The frontend is implemented in
`ui_server.py` and the backend is implemented in `modfirewall.c`.

The following is a diagram of how the webserver is connected to the system:

![](../images/firewall_webserver.svg)

The webserver component is connected to the internal ARP requester component due to some quirks with lwip,
the TCP/IP stack that we are using with micropython. We are using lwip with "lwip_ethernet" enabled, and thus
it wants to maintain its own ARP tables. When sending a packet out, it will first try to send out an ARP request
itself to properly construct the packet. However, as we have our own ARP components that are intercepting
all ARP traffic, we will never get the response back to lwip. Our *current* solution is to intercept the
ARP packets in `mpnetworkport.c` and first consulting the ARP tables, and if the entry does not exist, enqueue
a request to the requester component. Please see the `arp` README for more information.

As can be seen from the diagram, the webserver is connected to all the filters and both routers (only one is shown here).
The webserver makes protected procedure calls to each of these components when a new entry is added/modified/deleted. The
filtering rules and routes are stored in shared memory that is mapped into the webserver as read only, and mapped read/write
into their own respective component.