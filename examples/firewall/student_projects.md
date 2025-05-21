# Student Firewall Projects

## Preparation
- Microkit tutorial
- Firewall development workflow

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

arp_entry_state_t state;                    /* invalid, pending reply, unreachable, reachable */
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
syn attack protection/syn cookies, connected\established rules, flow control

#### TODO:


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


### Improved GUI
live traffic monitoring

#### TODO:


# Firewall Testing
- Echo server for simple packet filtering testing
- Running the firewall on machine queue
- Accessing both sides of the firewall for all boards (e.g. internal and external subnets)
- Access internal web server via an internal node
- Remotely update internal network node tables (e.g. IP forwarding)
- Monitor internal network node traffic (e.g. Wireshark)
- Respond to and generate traffic from the internal node of all types (UDP, TCP, ICMP, etc)
- Test internal network with more than one node
