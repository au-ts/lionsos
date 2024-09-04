#include "fatfs_config.h"
#include "co_helper.h"
#include <stdint.h>

static co_control_t co_controller_mem;
microkit_cothread_sem_t sem[WORKER_COROUTINE_NUM + 1];

void co_init(uint64_t* stack, uint32_t num) {
    microkit_cothread_init(&co_controller_mem, 
                            COROUTINE_STACKSIZE, 
                            stack[0], 
                            stack[1], 
                            stack[2], 
                            stack[3]);
    for (uint32_t i = 0; i < (WORKER_COROUTINE_NUM + 1); i++) {
        microkit_cothread_semaphore_init(&sem[i]);
    }
}

// This seems to not invalidate the handle when one coroutine finishes
void co_submit_task(void (*func)(void), void *args, co_handle_t *handle) {
    *handle = microkit_cothread_spawn(func, args);
}

void co_block() {
    microkit_cothread_ref_t handle = microkit_cothread_my_handle();
    microkit_cothread_semaphore_wait(&sem[handle]);
}

void co_kill() {
    microkit_cothread_ref_t handle = microkit_cothread_my_handle();
    microkit_cothread_destroy(handle);
}

void co_wakeup(co_handle_t handle) {
    microkit_cothread_semaphore_signal(&sem[handle]);
}

bool co_check_if_finished(co_handle_t handle) {
    co_state_t co_state = microkit_cothread_query_state(handle);
    return co_state == cothread_not_active;
}