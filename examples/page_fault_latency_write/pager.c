#include <microkit.h>
#include <stdint.h>
#include <stdlib.h>
#include <sddf/util/printf.h>
#include <sddf/benchmark/config.h>
#include <sddf/benchmark/bench.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>

#define ROUND_DOWN_TO_4K(x) ((x) & ~(4096 - 1))
#define BRK_START 0x8000000000 
#define MAX_PDS 64
#define NUM_PT_ENTRIES 300
#define MMAP_START 0x9000000000
typedef unsigned long long Cap;
typedef struct microkit_data {
    Cap frame_cap;
    uintptr_t pd_idx;
} frame_pd_id;

unsigned int vspaces[MAX_PDS];
uint64_t unmapped_frames_addr;
uint64_t num_frames;

// assumes that there is only one child.
inline Cap get_frame_cap(uintptr_t fault_addr) {
    return ((frame_pd_id *)(unmapped_frames_addr + ((ROUND_DOWN_TO_4K(fault_addr - BRK_START) / 4096) * sizeof(frame_pd_id))))->frame_cap;
}

__attribute__((__section__(".benchmark_client_config"))) benchmark_client_config_t benchmark_config;
benchmark_client_config_t *bench;
__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
serial_queue_handle_t serial_tx_queue_handle;

void init(void)
{
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);
}


seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    // sddf_notify(benchmark_config.start_ch);
    // not required.
    uintptr_t fault_addr = ROUND_DOWN_TO_4K(microkit_mr_get(1));
    uint64_t fsr = microkit_mr_get(2);
    bool is_write = (fsr >> 1) & 1;
    uint32_t pd_idx = child;

    if (is_write) {
        microkit_arm_page_map_rw(get_frame_cap(fault_addr), vspaces[pd_idx], ROUND_DOWN_TO_4K(fault_addr));
    } else {
        microkit_arm_page_map_ro(get_frame_cap(fault_addr), vspaces[pd_idx], ROUND_DOWN_TO_4K(fault_addr));
    }
    // sddf_notify(benchmark_config.stop_ch);
    return seL4_True;
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

