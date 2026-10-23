#include "pdcontrol/config.h"
#include <pdcontrol/ipc_tracer.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>

pdc_ipc_tracer_handle_t pdc_ipc_tracer_config_init(const pdc_config_handle_t *all_cfg,
                                                   const pdc_ipc_tracer_config_t *ipc_cfg)
{
    assert(all_cfg != NULL);
    assert(ipc_cfg != NULL);
    assert(strcmp(ipc_cfg->magic, PDC_IPC_CONFIG_MAGIC) == 0);

    return (pdc_ipc_tracer_handle_t) { .all_cfg = all_cfg, .ipc_cfg = ipc_cfg };
}
