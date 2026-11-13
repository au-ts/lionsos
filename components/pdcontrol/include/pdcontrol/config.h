/**
 * Basic configuration and the default debug info config.
 * Config structs hold information about traced PDs, such as their names and
 * their ids, and channels.
 * 
 * Can be used to make a nicer fault handler.
 */
#pragma once
#include <sel4/sel4.h>
#include <assert.h>

// all the config structs and initalisation stuff required.
#define PDC_MAX_CLIENTS 32
// maximum allowed pd id (inclusive).
// Higher pds ids are reserved for drivers and extensions.
#define PDC_MAX_PD_ID 32
#define PDC_MAX_CHANNELS 48
#define PDC_PD_MASK 0xff
#define PDC_CHANNEL_MASK 0x3f

#define PDC_BADGE_FAULT_BIT 62
#define PDC_BADGE_ENDPOINT_BIT 63

#define PDC_SCHED_PRIO 210
#define PDC_UNSCHED_PRIO 1
#define PDC_SENDER_PRIO 211
#define PDC_SELF_PRIO 212
#define PDC_BLOCK_PRIO SCHED_PRIO

#define PDC_INPUT_CAP 1
#define PDC_REPLY_CAP 4
#define PDC_MAX_NAME_LEN 64
#define PDC_MICROKIT_CALL_BADGE_MASK 0xff
#define PDC_UNSET_VALUE ((seL4_Word)(-1))

#define PDC_BADGE_FAULT_BIT 62
#define PDC_BADGE_ENDPOINT_BIT 63

static char PDC_PD_CONFIG_MAGIC[] = "pdmagic";
static char PDC_CHANNEL_CONFIG_MAGIC[] = "chanmagic";
static char PDC_IPC_CONFIG_MAGIC[] = "ipcmagic";
// Notifications is the number of leading zeros.

typedef enum pdc_ipctype {
    pdc_ipctype_SenderReply = 1,
    pdc_ipctype_Call,
    pdc_ipctype_Ntfn,
    pdc_ipctype_Fault,
    pdc_ipctype_Send,
    pdc_ipctype_BlockChecker,
} pdc_ipctype_e;

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
    const pdc_pd_all_config_t *pd_config;
    const pdc_channel_all_config_t *chan_config;
} pdc_config_handle_t;

// chan_config is nullable. pd_config is not.
pdc_config_handle_t pdc_config_init(pdc_pd_all_config_t *pd_config, pdc_channel_all_config_t *chan_config);

static inline seL4_Word pdc_badge_to_channel_id(seL4_Word badge)
{
    unsigned int idx = 0;
    do {
        if (badge & 1) {
            break;
        }
        badge >>= 1;
        idx++;
    } while (badge != 0);
    return idx;// __builtin_ctz(badge);
}

static inline pdc_channel_config_t pdc_channel_to_child_id(const pdc_config_handle_t *cfg, seL4_Word chan)
{
    return cfg->chan_config->configs[chan];
}

static inline pdc_ipctype_e pdc_badge_to_ipctype(seL4_Word badge)
{
    seL4_Word is_endpoint = badge >> PDC_BADGE_ENDPOINT_BIT;
    seL4_Word is_fault = (badge >> PDC_BADGE_FAULT_BIT) & 1;

    if (is_endpoint) {
        return pdc_ipctype_Call;
    };
    if (is_fault)
        return pdc_ipctype_Fault;
    return pdc_ipctype_Ntfn;
}

static inline seL4_Word pdc_badge_to_channel(seL4_Word badge)
{
    switch (pdc_badge_to_ipctype(badge)) {
    case pdc_ipctype_SenderReply:
        assert(!"TODO");
        break;
    case pdc_ipctype_Call: {
        return badge & PDC_PD_MASK;
    }
    case pdc_ipctype_Ntfn: {
        unsigned int idx = 0;
        do {
            if (badge & 1) {
                break;
            }
            badge >>= 1;
            idx++;
        } while (badge != 0);
        return idx;// __builtin_ctz(badge);
    }
    case pdc_ipctype_Fault:
        assert(!"TODO");
        break;
    case pdc_ipctype_Send:
        assert(!"TODO");
        break;
    case pdc_ipctype_BlockChecker:
        assert(!"TODO");
        break;
    default:
        assert(!"Unknown badge");
    }
}
