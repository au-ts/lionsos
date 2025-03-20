#include <stdio.h>

#define MAX_MAC_LEN        20
#define MAX_CIDR_LEN       30
#define MAX_PROTOCOL_LEN   20
#define MAX_IFACE_STR_LEN  64

typedef struct {
    char mac[MAX_MAC_LEN];
    char cidr[MAX_CIDR_LEN];
} webserver_interface_t;

typedef struct {
    uint64_t id;
    char destination[MAX_CIDR_LEN];
    char gateway[MAX_CIDR_LEN];
    int interface;
} webserver_routing_entry_t;
