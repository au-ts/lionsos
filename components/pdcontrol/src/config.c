#include "pdcontrol/config.h"
#include <sel4/sel4.h>
#include <pdcontrol/pdcontrol.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <sddf/util/printf.h>

static inline void pdc_pd_all_config_print(const pdc_pd_all_config_t* config) {
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

static inline void pdc_channel_all_config_print(const pdc_channel_all_config_t* config) {
    sddf_dprintf("channel_config:\n");
    sddf_dprintf("    magic:       %s\n", config->magic);
    sddf_dprintf("    num_channels: %lu\n", config->num_channels);
    sddf_dprintf("    configs:\n");
    for (int i = 0; i < config->num_channels; i++) {
        const pdc_channel_config_t* child = config->configs + i;
        sddf_dprintf("        channel %d:\n", i);
        sddf_dprintf("            end_a_channel_id: %lu\n", child->end_a_channel_id);
        sddf_dprintf("            end_b_channel_id: %lu\n", child->end_b_channel_id);
        sddf_dprintf("            end_a_child_id:   %lu\n", child->end_a_child_id);
        sddf_dprintf("            end_b_child_id:   %lu\n", child->end_b_child_id);
    }
}

pdc_config_handle_t pdc_config_init(pdc_pd_all_config_t* pd_config, pdc_channel_all_config_t *chan_config) 
{
    assert(pd_config != NULL);
    assert(chan_config != NULL);
    assert(strcmp(pd_config->magic, PDC_PD_CONFIG_MAGIC) == 0);
    assert(strcmp(chan_config->magic, PDC_CHANNEL_CONFIG_MAGIC) == 0);
    pdc_pd_all_config_print(pd_config);
    pdc_channel_all_config_print(chan_config);
    return (pdc_config_handle_t){ .pd_config=pd_config, .chan_config=chan_config };
}
