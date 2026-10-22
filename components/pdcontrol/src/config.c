#include "pdcontrol/config.h"
#include <sel4/sel4.h>
#include <pdcontrol/pdcontrol.h>
#include <stddef.h>
#include <assert.h>
#include <sddf/util/printf.h>

static inline void pdc_all_config_print(const pdc_pd_all_config_t* config) {
    sddf_dprintf("pdc_config:\n");
    sddf_dprintf("    magic:       %s\n", config->magic);
    sddf_dprintf("    num_clients: %lu\n", config->num_clients);
    sddf_dprintf("    configs:\n");
    for (int i = 0; i < config->num_clients; i++) {
        const pdc_pd_config_t* child = config->configs + i;
        sddf_dprintf("        client %d:\n", i);
        sddf_dprintf("            name: %s\n", child->name);
        sddf_dprintf("            prio: %lu\n", child->def_prio);
        sddf_dprintf("            id:   %lu\n", child->id);
    }
}

pdc_config_handle_t pdc_config_init(pdc_pd_all_config_t* pd_config, pdc_channel_config_t *chan_config) 
{
    assert(pd_config != NULL);
    pdc_all_config_print(pd_config);
    return (pdc_config_handle_t){ .pd_config=pd_config, .chan_config=chan_config };
}
