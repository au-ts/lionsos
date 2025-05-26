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

fw_arp_entry_state_t state;                    /* invalid, pending reply, unreachable, reachable */
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

#### TODO:
- Read about cuckoo hashes
- Create a flush arp table function


### Improved TCP
syn attack protection/syn cookies
connected\established rules
monitoring network flow
TCP module: When a TCP connection attempts a 3-way handshake with a closed port then a RST packet should be sent back as a response. See RFC793.

#### TODO:
- Write this

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

#### TODO:
- Read this and Krishnan's code changes


### Improved GUI

- Remove valid entry from filter and routing table
- Add doubly linked list structure to routing and filter tables. Optimise searches through the tables.
- Live traffic monitoring

#### TODO:
- Write this


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
ssh {yourTSusername}@fwp-int-[1345]-1.keg.cse.unsw.edu.au 'dumpcap -F pcap -i internal -i enp4s0 -w - -f "not port 22"' | wireshark -k -i -
```

   The `-i internal` and `-i enp4s0` arguments specify that traffic on both the internal and keg
   interfaces is outputted, but feel free to omit `-i enp4s0`. We will ensure that everyone has the
   correct permissions to run `dumpcap` on each internal node on your first day.

4. The firewall provides a webserver interface to view, modify and create firewall routing rules and
   traffic filtering rules. It also displays firewall interface details. The webserver can only be
   accessed from the internal network, thus to acces the webserver, you must tunnel your requests
   and responses through the corresponding internal node. Upon arrival, Alex (our sys admin) will
   ensure that your ssh config is set up so that you can access each firewall webserver with your
   browser at `localhost:8080`.
