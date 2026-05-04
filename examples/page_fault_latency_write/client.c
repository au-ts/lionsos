#include <microkit.h>
#include <stdint.h>
#include <stdlib.h>
#include <sddf/util/printf.h>
#include <sddf/benchmark/config.h>
#include <sddf/benchmark/bench.h>

#define BRK_START 0x8000000000 

// I should give it just one page.

__attribute__((__section__(".benchmark_client_config"))) benchmark_client_config_t benchmark_config;
benchmark_client_config_t *bench;


void init(void)
{
    bench = (benchmark_client_config_t *)benchmark_config;
    setup_utilization_socket(&benchmark_config);
    // create a unbacked variable.
    int *i = BRK_START;
    sddf_notify(bench->start_ch);
    // start benchmark
    // attempt write on unbacked page.
    *i = 1;
    // end benchmark
    sddf_notify(bench->stop_ch);
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