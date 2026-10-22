#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <lions/firewall/bridge.h>
#include <lions/firewall/config.h>

__attribute__((__section__(".fw_bridge_config"))) fw_bridge_config_t config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;

static fw_fdb_entry_t scratch[FW_FDB_NUM_ENTRIES];
static fw_fdb_entry_t entries[FW_FDB_NUM_ENTRIES];

static uint64_t ages_to_clean = FDB_AGES_PER_CLEAN;

void notified(microkit_channel ch)
{
    if (ch == timer_config.driver_id) {
        fw_fdb_age(entries);

        ages_to_clean--;
        if (ages_to_clean == 0) {
            fw_fdb_clean(entries, scratch);
            ages_to_clean = FDB_AGES_PER_CLEAN;
        }

        sddf_timer_set_timeout(timer_config.driver_id, FDB_AGEING_PERIOD_NS);
    }
}

void init(void)
{
    fw_fdb_init(entries);
    sddf_timer_set_timeout(timer_config.driver_id, FDB_AGEING_PERIOD_NS);
}
