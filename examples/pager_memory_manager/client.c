#include <microkit.h>
#include "types.h"
#include <stdint.h>
#include <stdlib.h>
#include <sddf/util/printf.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/config.h>
#include <sddf/util/cache.h>
#include <lions/fs/helpers.h>
#include <lions/fs/config.h>
#include <lions/fs/protocol.h>
#include <lions/posix/posix.h>
#include <libmicrokitco.h>
__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

// NOT USED BELOW:
seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    // this is not used
    seL4_MessageInfo_t ret;
    return ret;
}



void notified(microkit_channel ch)
{
    // this may not be required
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    // not required.
    return seL4_False;
}

#define NUMMAPS 256
#define PAGE_SIZE 4096

void init(void)
{
    sddf_printf("hello from client\n");
}