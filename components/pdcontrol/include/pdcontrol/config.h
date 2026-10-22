#pragma once
#include <sel4/sel4.h>

// all the config structs and initalisation stuff required.
#define PDC_MAX_CLIENTS 32
// maximum allowed pd id (inclusive).
// Higher pds ids are reserved for drivers and extensions.
#define PDC_MAX_PD_ID 32
#define PDC_MAX_CHANNELS 48

#define PDC_INPUT_CAP 1
#define PDC_REPLY_CAP 4
#define PDC_MAX_NAME_LEN 64
#define PDC_MICROKIT_CALL_BADGE_MASK 0xff

static char PDC_PD_CONFIG_MAGIC[] = "pdmagic";
static char PDC_CHANNEL_CONFIG_MAGIC[] = "chanmagic";
// Notifications is the number of leading zeros.

typedef struct {
    char name[PDC_MAX_NAME_LEN];
    seL4_Word def_prio;
    seL4_Word id; // id is from the perspective of a controller.
} pdc_pd_config_t;

// The channel from the perspective of an interceptor
typedef struct {
    seL4_Word end_a_channel_id;
    seL4_Word end_b_channel_id;
    // child_id is from the perspective of the controller.
    seL4_Word end_a_child_id;
    seL4_Word end_b_child_id;
} pdc_channel_config_t;

// required
typedef struct {
    char magic[sizeof(PDC_PD_CONFIG_MAGIC)];
    seL4_Word num_clients;
    pdc_pd_config_t configs[PDC_MAX_CLIENTS];
} pdc_pd_all_config_t;

typedef struct {
    char magic[sizeof(PDC_CHANNEL_CONFIG_MAGIC)];
    seL4_Word num_channels;
    // sus?
    pdc_channel_config_t configs[PDC_MAX_CHANNELS];
} pdc_channel_all_config_t;

// contains necessary maps for querying efficiently.
typedef struct {
    const pdc_pd_all_config_t* pd_config;
    const pdc_channel_all_config_t* chan_config;
} pdc_config_handle_t;

// chan_config is nullable. pd_config is not.
pdc_config_handle_t pdc_config_init(pdc_pd_all_config_t* pd_config, pdc_channel_all_config_t* chan_config);
