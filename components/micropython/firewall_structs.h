#include <stdio.h>

#define MAX_MAC_LEN        20
#define MAX_CIDR_LEN       30
#define MAX_PROTOCOL_LEN   20
#define MAX_IFACE_STR_LEN  64

typedef struct {
    char mac[MAX_MAC_LEN];
    char cidr[MAX_CIDR_LEN];
} interface_t;

typedef struct {
    uint64_t id;
    char destination[MAX_CIDR_LEN];
    char gateway[MAX_CIDR_LEN];
    int interface;
} routing_entry_t;

typedef struct {
    uint64_t id;
    char protocol[MAX_PROTOCOL_LEN];
    char iface1[MAX_IFACE_STR_LEN];
    char iface2[MAX_IFACE_STR_LEN];
} firewall_rule_t;
