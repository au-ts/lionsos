#include "microkit.h"
#include <stdbool.h>
#include <stdint.h>
#include <os/sddf.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <sddf/network/queue.h>
#include <sddf/network/config.h>
#include <lions/firewall/checksum.h>
#include <lions/firewall/config.h>
#include <lions/firewall/common.h>
#include <lions/firewall/filter.h>
#include <lions/firewall/ip.h>
#include <lions/firewall/udp.h>
#include <lions/firewall/queue.h>

__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;
__attribute__((__section__(".fw_nat_config"))) fw_nat_config_t nat_config;

/* Incoming packets from filter */
fw_queue_t filter_queue;

/* Outgoing packets to router */
fw_queue_t router_queue;

seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo) {}

void notified(microkit_channel ch) {
    net_buff_desc_t buffer;

    if (ch == nat_config.filter.ch) {
        /* Incoming packet from filter */
        int err = fw_dequeue(&filter_queue, &buffer);
        assert(!err);

        sddf_printf("[UDP NAT] intercepted packet");

        fw_enqueue(&router_queue, &buffer);
        assert(!err);

        microkit_notify(nat_config.router.ch);
    }
}

void init(void) {
     sddf_printf("[UDP NAT] Starting...\n");

     sddf_printf("router queue vaddr: %p, capacity: %d\n", nat_config.router.queue.vaddr, nat_config.router.capacity);
     fw_queue_init(&router_queue, nat_config.router.queue.vaddr,
         sizeof(net_buff_desc_t),  nat_config.router.capacity);

     sddf_printf("filter queue vaddr: %p, capacity: %d\n", nat_config.filter.queue.vaddr, nat_config.filter.capacity);
     fw_queue_init(&filter_queue, nat_config.filter.queue.vaddr,
         sizeof(net_buff_desc_t),  nat_config.filter.capacity);
}
