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

seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo) {}

void notified(microkit_channel ch) {}

void init(void) {
     sddf_printf("[UDP NAT] Starting...\n");
}
