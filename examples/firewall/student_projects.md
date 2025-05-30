# Student Firewall Projects

## Preparation
- Microkit tutorial

## Building and Running the Firewall
Please see the [firewall README.md](/examples/firewall/README.md) for details on how to build and
run the firewall.

## Configuration Structs
As a product of building the firewall, a python module
[config_structs.py](/examples/firewall/config_structs.py) will be generated in the firewall example
directory. This module is generated from C config headers (e.g.
[firewall configs](/include/lions/firewall/config.h)) which are a means to pass Microkit system
information to components after they have been built. Components will declare the configuration
structs they need at the top of their C files, and once the Microkit objects have been created in
the [metaprogram](/examples/firewall/meta.py), their values will be written to `.data` files in the
build directory, and they will be obj-copied into each `.elf` file. Config declarations look like
this:

```c
__attribute__((__section__(".fw_arp_requester_config"))) fw_arp_requester_config_t arp_config;
```

Ensuring the data files match the precise format that the C components are expecting can be
challenging, thus we developed a python module
[sdfgen_helper.py](/examples/firewall/sdfgen_helper.py) which creates Python-to-C conversion data
types to be used in the metaprogram.

If you wish to change or create new configuration structs, the best way to update the metaprogram is
to first re-run the sdfgen helper module on the new C header files, and then use the outputted
config structs module as a reference.

The sdfdgen helper module is a recent addition to the firewall system, so if you encounter any
problems with it please let us know.

## Projects

### Implementing a Cuckoo hash table to store ARP entries

See here: https://en.wikipedia.org/wiki/Cuckoo_hashing

In order for components to find the corresponding MAC address to an IP address, the firewall system
keeps track of a collection of IP address and MAC address pairs. These pairs are stored in the
`arp_table` which currently is a very simple data structure. New entries are placed in the next
available table slot, but the entire table must be searched during insertion to ensure that there is
not an existing entry for this IP. Retrieving and removing entries is slightly more efficient, as
when a match is found it can be returned immediately, however all operations are O(table capacity).
The goal of this project is to upgrade this table implementation to a cuckoo hash table which
provides an efficient method of handling hash collisions.

The routing components (`routing_internal.c` and `routing_external.c`) have read only access to the
arp table, and use it when routing packets through to the other network. When a packet is received,
the destination IP address is used as a key to the arp table to find the corresponding MAC address.
If an entry is found, the MAC address is substituted into the outgoing packet. If an entry is not
found, the routers will generate an `arp_request` and inserted it into a shared queue with the arp
requester which is connected to the outgoing network. When the arp requester processes the queue and
finds the arp request entry, it will send out an arp request to the outgoing network to resolve the
requested IP address.

The webserver component also shares an arp queue with the internal network arp requester, however it
does not have access to the arp table and instead uses lwip to keep track of it's own
correspondences.

When the arp requester receives a positive response for a request it send out, it will both update
the arp table as well as enqueue an arp request response to the client who originated the request.
Once the requester sends out a request, it periodically checks whether it has received a response.
For every 5 seconds that it does not receive a response, it will resend the request up to a maximum
of 5 retries. If no response is obtained, it will mark the arp entry as unreachable. The arp table
is also completely flushed out (all entries removed) periodically as well, currently set to hourly.
This ensures that stale entries are not stored indefinitely.

Thus, our arp table entries need to keep track of the following fields:

fw_arp_entry_state_t state;                 /* invalid, pending reply, unreachable, reachable */
uint32_t ip;                                /* IP of entry */
uint8_t mac_addr[ETH_HWADDR_LEN];           /* MAC of IP */
uint8_t client;                             /* client where the request originates */
uint8_t num_retries;                        /* number of retries */

However, notice that once the mac address is obtained or the IP is found to be unreachable, we no
longer need to store the client and number of retries field, and similarly before a response has
been received, the MAC address field is meaningless. So, we hope that we will be able to fit these
fields into a single cache line, further optimising our cuckoo hash table.

To implement the hash table, we imagine the vast majority of the code changes will be in the
[arp_queue](/include/lions/firewall/arp_queue.h) file where our current implementation is. This
includes the table data structure itself as well as the functions used to initialise, look up,
insert and flush (remove all entries). If additional changes are needed, this is fine. The four
protection domains that use the arp table and queues for creating requests are:
- [webserver](/components/micropython/mpnetworkport.c)
- [internal router](/examples/firewall/routing_internal.c)
- [external router](/examples/firewall/routing_external.c)
- [arp requester](/examples/firewall/arp_requester.c)


### Improved TCP
Currently our [TCP filter component](/examples/firewall/filters/tcp_filter.c), which monitors all
TCP traffic, is not implemented with any distinction from our UDP and ICMP filter components. The
goal of this project is to change this, and implement some TCP specific filtering functionalities.

The first potential functionality is better TCP connection tracking. Currently we do implement a
`CONNECT/ESTABLISHED` filtering rule where traffic is permitted only if it has been seen in the
other direction. If traffic matches with a `CONNECT` rule (which is possibly the filter's default
rule) the source and destination IP and port are written to a memory region shared with the other
filter. The region which a filter writes to is called the internal instances region, and the region
it reads from is called the external instances region. Whenever a filter component receives traffic,
it first checks its external instances region before checking its rules, and if a match is found the
traffic will be allowed. Care is taken so that if a filter's `CONNECT` rule is changed, all
instances generated from this rule are removed. Since TCP traffic involves a 3-way handshake before
a connection is established, we hope to implement a more complex TCP `CONNECT/ESTABLISH` rule that
only considers a connection established once the TCP handshake has completed. This would require the
TCP filter to perform more complex book-keeping. 

Once we have the infrastructure to track TCP handshakes, this would enable the TCP filter to
implement protection from syn attacks. This could possible be implemented with SYN cookies.

Another possible improvement to the TCP filter is correctly handling TCP handshake error conditions.
When a TCP connection attempts a 3-way handshake with a closed port then a TCP RST packet should be
sent back as a response (See RFC793). For our TCP filter to handle this case, it will require the
ability to send packets out the interface it is filtering (which it is currently unable to do). As a
first implementation, we can add the infrastructure to allow the filter to send packets directly,
but an improved implementation would involve the TCP filter contacting another component that has
network interface transmit access, possibly the ICMP module component. This would also involve
creating new data structures to pass the relevant information between components.


### ICMP
For an overview of different types of ICMP messages see:
https://www.computernetworkingnotes.com/networking-tutorials/icmp-error-messages-and-format-explained.html.

There is a basic skeleton ICMP module currently that is intended to send "destination unreachable"
packets back to a source in the case that an ARP request for the destination IP address times out.
We wish to extend the functionality of this module. Additionally, there is an ICMP filter that
behaves the same as the TCP and UDP packet filters, with rules tables that allow us to decide
when to accept/drop packets.

#### Pings
We want our firewall to be able to mediate if Pings are allowed through the firewall from the external
network into the internal. We wish to add a configuration option to the firewall allowing us to specify
this. If not allowed, we will drop any unsolicited pings coming into the firewall from the external
network. However, if a device on the external network sends a ping out to the external network,
we want the reply to be let back in. This will require tracking of outgoing pings, and filtering
which ones we let back in.

This will largely involve modifying `icmp_filter.c`, and creating a shared memory region between them
so they can track the flow of pings, as well as if we should drop all ping packets. This *may*
be covered with the already exisiting "connect establish" rules, in which we can make it a wildcard
rule by using a subnet length of 0.

#### Rejecting packets
Currently, the filtering components only support allowing messages through the firewall, or dropping them.
However, there is a third option that is sometimes used, and that is "reject". TCP and UDP handle rejections
differently. For TCP, you should send out a reset, "RST", packet. For UDP, we send an ICMP "Port Unreachable"
packet. We wish to implement the latter. We will need to somehow communicate from the UDP filter to the ICMP
module to generate this Port Unreachable packet.
See here for more information: https://www.coresentinel.com/reject-versus-drop/.

As an extension, if there is time we can implement the TCP reset packet generation in the ICMP module
for now.

#### Destination Unreachable
Currently, we only generate "Destination Host Unreachable" packets if an ARP request times out. We should,
however, distinguish between "Host Unreachable" and "Network Unreachable". We should send "Host Unreachable"
only for devices connected directly to our router, where our router is the last along the path. And we should
send "Network Unreachable" packets if the next hop for the packet is a router.
Some more information can be found here: https://www.liveaction.com/glossary/internet-control-message-protocol-icmp/.

#### TTL Expiry
In our routing component, we decrement the "time to live" field in the IP packets. When we reach 0,
we currently just drop the packet. However, we would also wish for the routing component to signal
the ICMP module to generate a "Time Exceeded" message.

Note: Alot of this work will involve broadening the interface between the router (and other components)
and the ICMP module. Additionally, we only generate a destination unreachable packet in the ICMP module,
so developing a more generic packet constructer is required.


### Improved GUI
Currently our firewall webserver GUI is functional, however fairly basic. This project invovles two
types of changes: back-end and front-end.

For back-end changes, we would like to implement a creation-based ordering on our routes and filter
rules. Currently routes and rules are stored in tables, and are assigned unique IDs by the index of
their entry. There is no gaurantee that routes and rules will be stored consecutively within their
tables since they may be deleted, so we must always search the entire table when finding matches and
displaying them in the webserver. Obviously this is inefficient! The first improvement that can be
made is to add a doubly linked list ordering into the tables, so we need only loop through valid
entries when searching. Preferably this could be implemented in the style of the firewall, which
would be `prev` and `next` integers storing the index of the previous and next entries. This would
both vastly improve searching the tables, as well as allow us to display the entries in the order
which they were created.

Once this is implemented, as a follow improvement we would like to remove any `valid` booleans from
our entries which we are currently using to verify whether an entry is in use, and instead use one
of the existing fields to determine this.

Currently the front-end implementation of the webserver is very elementary and difficult to use,
with embedded javascript inside a python file. Unfortunatley nobody in the firewall project has much
front-end experience, so we would be open to any improvements on this! Our webserver component uses
micropython, and we use microdot as our web framework. As well as improving our codebase structure,
we would also welcome improvements to the appearance of the website itself. 

Finally, a functionality that would involve both front and back end changes would be to create a new
webserver page which displays live traffic monitoring in both directions. This would require
additional shared memory, as well as more logging in the network components. Basic logging would be
fairly simple to implement, however a more functional system may involve the use of timestamps to
track how long entries should be stored. Since we already have shared memory mechanisms set up for
routes and filter rules, this can be used as a starting point. Similarly, we already have example
webserver endpoints for displaying entries in these regions. If the project is too complex, you are
welcome to implement only the front-end (with dummy data) or the back-end (using pre-existing serial
output to display the logs).


# Testing the Firewall

1. The first step to test the firewall is to run the firewall on an iotgate. Choose which iotgate
   you will be using first, and then pass it's number to the build system by setting the environment
   variable: `FW_IOTGATE_IDX=[1345]`. This is used to set the correct MAC and IP addresses for the
   external and internal interfaces, which are used by the firewall components.

   The `FW_IOTGATE_IDX` argument is passed to the firewall [metaprogram](/examples/firewall/meta.py)
   and is used as an index to select MAC and IP addresses in the `macs` and `ips` Python lists at
   the top of the program. The MAC addresses and IPs for each interface are also listed in the
   [interfaces](/examples/firewall/interfaces.md) document.


2. The external firewall interfaces are part of the TFTP network (172.16.0.2/16), and the internal
   interfaces are part of a small internal network (192.168.[1345].2/24) with only two nodes - the
   firewall which can be found at .1, and an internal virtual machine node which can be found at .2.
   The internal virtual machine also has a connection to the keg network (10.13.0.191/23), however
   is configured only to send and receive traffic on this interface that is related to debugging.
   The firewall can be tested by sending traffic through in either direction.

   The TFTP network is set up to forward any traffic with destination IP in one of the internal
   networks to the corresponding external iotgate interface. For example, if a packet of the form:

   (172.16.0.5) --> (192.168.1.2)

   if sent out on the TFTP network, it will be redirected as follows:

   (172.16.0.5) --> iotgate1 external interface (172.16.2.1) --> iotgate1 external interface --> (192.168.1.2)

   this involves updating the MAC address of the packet to the MAC of the external iotgate, but the
   destination IP is left unchanged so the firewall can forward it to its ultimate destination.

   Similarly, the internal VM node is configured to send out all non-debug traffic through its
   firewall internal interface, so any TFTP traffic will first be routed to the internal firewall
   interface via MAC address substitution.

   To test external --> internal traffic, first ssh into TFTP (ensure you are connected to the keg
   network), then send ICMP, UDP or TCP traffic to the internal node as follows:

```sh
ssh tftp
ping 192.168.1.2
nc -u 192.168.1.2 port
nc 192.168.1.2 port
```
   netcat (nc) can be used to send out either UDP or TCP traffic.

   To test internal --> external traffic, first ssh into the internal node (you may use hostnames
   listed in the [interfaces](/examples/firewall/interfaces.md) file), and then send ICMP, UDP or
   TCP traffic as above:

```sh
ssh fwp-int-1-1
ping 172.16.0.2
nc -u 172.16.0.2 port
nc 172.16.0.2 port
```

   Currently the firewall does not handle traffic addressed to itself correctly (with the exception
   of ARP requests for the firewall's IPs and webserver traffic which is explained below). If
   traffic destined for the firewall is received, the firewall assumes it is destined for another
   node, and will send out an ARP request for its MAC. This does not cause any errors, and will
   ultimately result in the traffic being dropped, so can be used to test things if desired.


3. The firewall will provide plenty of debug output for monitoring traffic on the interfaces, but
   wireshark can also be used on the internal VM node. Currently students do not have permissions to
   run wireshark on TFTP, however this is possible if this is needed. To run wireshark on the node,
   it is reccomended to run the wireshark client locally, and tunnel `dumpcap` output from the VM in
   to your client. This can be done simply using the following command:

```sh
ssh @fwp-int-[1345]-1.keg 'dumpcap -F pcap -i internal -w - -f "not port 22"' | wireshark -k -i -
```

   The `-i internal` and `-i enp4s0` arguments specify that traffic on both the internal and keg
   interfaces is outputted, but feel free to omit `-i enp4s0`. We will ensure that everyone has the
   correct permissions to run `dumpcap` on each internal node on your first day.

4. The firewall provides a webserver interface to view, modify and create firewall routing rules and
   traffic filtering rules. It also displays firewall interface details. The webserver can only be
   accessed from the internal network, thus to acces the webserver, you must tunnel your requests
   and responses through the corresponding internal node. To enable this, the following rule must be
   added to you ssh config file:

```sh
Host fwp-int-?-1
      ProxyJump login.trustworthy.systems
      User courtneyd
      IdentityFile ~/.ssh/ts_rsa
      Hostname %h
      LocalForward 8080 localhost:80
```

   You should already have a rule similar to this for matching on internal nodes, however be sure to
   add the `LocalForward` part.

   Then, when you wish to connect to the webserver, run

```sh
ssh -fN fwp-int-?-1
```
   in your terminal. This command does not create output. This sets up traffic forwarding with the
   internal node. To access the webserver, go to `localhost:8080`.

   To close your traffic forwarding connection with the internal node, close the terminal you used
   to perform the ssh.
