#pragma once
#include "config.h"
#include <stdint.h>

typedef struct {
    char magic[sizeof(PDC_IPC_CONFIG_MAGIC)];
    uint8_t channel_id_to_source_child_id[PDC_MAX_CHANNELS];
} pdc_ipc_tracer_config_t;

typedef struct {
    const pdc_config_handle_t* all_cfg;
    const pdc_ipc_tracer_config_t* ipc_cfg;
} pdc_ipc_tracer_handle_t;
