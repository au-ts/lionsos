# ARP Components

We have two distinct ARP components, the ARP requester which is responsible for handling ARP requests and
maintaing ARP tables, and the ARP responder, which simply responds to ARP requests that have been addressed
to the NIC it is attached to.

There are two of each of these components, one set for inbound traffic and one for outbound traffic.

## ARP Requester

The ARP requester receieves ARP requests from the router, and if the ARP requester for handling
ARP requests for the internal network, also from the webserver component. These requests are of
the following structure:

```c
typedef struct arp_request {
    uint32_t ip;                        /* Requested IP */
    uint8_t mac_addr[ETH_HWADDR_LEN];   /* Zero filled or MAC of IP */
    arp_entry_state_t state;            /* State of this ARP entry */
} arp_request_t;
```

On receipt of a request, the ARP component will try to send out an ARP request
to the network. It will also fill out an entry into the ARP tables of the following structure:

```c
typedef struct arp_entry {
    arp_entry_state_t state;                    /* State of this entry */
    uint32_t ip;                                /* IP of entry */
    uint8_t mac_addr[ETH_HWADDR_LEN];           /* MAC address of IP */
    uint8_t client;                             /* Bitmap of clients that initiated the request */
    uint8_t num_retries;                        /* Number of times we have sent out an arp request */
    uint64_t timestamp;                         /* Time of insertion */
} arp_entry_t;
```

After a specified timeout, we will retry the request. If we reach a threshold of retries,
we will pass through an error to the requesting component.
If we get an ARP response, as passed through the rx virtualiser, we will populate the ARP
table entry, mark it as valid and enqueue a response to the requesting component. These responses
are of the `arp_request_t` type, and will hold the following values in the `arp_entry_state_t state` enum:

```c
typedef enum {
    ARP_STATE_INVALID,                  /* Whether this entry is valid entry in the table */
    ARP_STATE_PENDING,                  /* Whether this entry is still pending a response */
    ARP_STATE_UNREACHABLE,              /* Whether this ip is reachable and listed mac has meaning */
    ARP_STATE_REACHABLE
} arp_entry_state_t;
```

After a certain time interval, we will also completely flush the ARP cache to ensure that there are no
stale entries. The following are constants from `arp_requester.c` that the user can configure:
```c
#define ARP_MAX_RETRIES 5               /* How many times the ARP requester will send out an ARP request. */
#define ARP_RETRY_TIMER_S 5             /* How often to retry an ARP request, in seconds. */
#define ARP_CACHE_LIFE_M 5              /* The lifetime of the ARP cache in minutes. After this time elapses, the cache is flushed. */
```
The following is a diagram with the ARP requster connected to the webserver:

![](../images/arp_requester.svg)

## ARP Responder

The ARP responder is a simple component. It simply takes a statically defined IP address
and MAC address, as outputted as config structures from the meta program. If the system
receives an ARP request, it is passed onto the ARP responder component. If it is addressed
for our given IP address, we simply respond with our given MAC address.

![](../images/arp_responder.svg)