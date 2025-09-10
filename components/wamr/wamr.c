#include <microkit.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/config.h>
#include <sddf/timer/client.h>
#include <sddf/network/lib_sddf_lwip.h>
#include <lions/posix/posix.h>

#include <wasm_export.h>

#include <stdio.h>

#include "wamr.h"

#define TIMEOUT (1 * NS_IN_MS)

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;
__attribute__((__section__(".lib_sddf_lwip_config"))) lib_sddf_lwip_config_t lib_sddf_lwip_config;

serial_queue_handle_t serial_tx_queue_handle;

void notified(microkit_channel ch) {}

void init(void) {
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);
    syscalls_init();
}
