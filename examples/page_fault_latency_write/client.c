#include <microkit.h>
#include <stdint.h>
#include <stdlib.h>
#include <sddf/util/printf.h>
#include <sddf/benchmark/config.h>
#include <sddf/benchmark/bench.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>

#define BRK_START 0x8000000000 

// I should give it just one page.

__attribute__((__section__(".benchmark_client_config"))) benchmark_client_config_t benchmark_config;
benchmark_client_config_t *bench;
__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
serial_queue_handle_t serial_tx_queue_handle;
void init(void)
{

    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);
    // serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);
    // bench = (benchmark_client_config_t *)benchmark_config;

    // create a unbacked variable.
    int *i = (int *)BRK_START;
    sddf_notify(benchmark_config.start_ch);
    // start benchmark
    // attempt write on unbacked page.
    *i = 1;
    // end benchmark
    sddf_notify(benchmark_config.stop_ch);
    assert(*i == 1);
}


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