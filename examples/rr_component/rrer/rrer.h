#include <sel4/sel4.h>
#include <microkit.h>
#include <sddf/util/printf.h>
#define LOG(...) sddf_printf("RRER | " __VA_ARGS__)

typedef enum {
    rr_ScheduleState_Schedulable = 0,
    rr_ScheduleState_Scheduled,
    rr_ScheduleState_Blocked,
    _rr_ScheduleState_ = 1 << 63, // force to be seL4_Word size
} rr_ScheduleState_e;

typedef struct {
    seL4_Word id;
    seL4_Word priority;
    rr_ScheduleState_e sched_state;
} rr_Child_t;

static inline void rrer_main();
static inline void rrer_init(void* data, size_t data_size_bytes);

/* STATE */
rr_Child_t* children_arr = NULL;
seL4_Word children_num = 0;

// The index is the id. Points to mr_prefilled data.
static inline void rrer_init(void* data, size_t data_size_bytes) {
    assert(data_size_bytes % sizeof(rr_Child_t) == 0);
    // initialises the children.
    children_arr = data;
    children_num = data_size_bytes / sizeof(rr_Child_t);
    LOG(
        "children_arr: %p, children_num: %lu\n", 
        children_arr,
        children_num
    );
}

// main
static inline void rrer_main() {
    for (int i = 0; i < children_num; i++) {
        LOG(
            "id: %lu, priority: %lu, sched_state: %lu\n", 
            children_arr[i].id,
            children_arr[i].priority,
            (seL4_Word)children_arr[i].sched_state
        );
    }
}
