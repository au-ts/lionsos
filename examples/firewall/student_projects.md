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


### ICMP Module
pings, destination unreachable, what else?
choose 1 or 2 simple functionalities

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
