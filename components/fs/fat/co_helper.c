#include "fatfs_config.h"
#include "co_helper.h"

#ifdef USE_FIBERPOOL
void co_init(uint64_t* stack, uint32_t num) {
    struct stack_mem stackmem[4];
    stackmem[0].memory = (void*)stack[0];
    stackmem[0].size = COROUTINE_STACKSIZE;
    stackmem[1].memory = (void*)stack[1];
    stackmem[1].size = COROUTINE_STACKSIZE;
    stackmem[2].memory = (void*)stack[2];
    stackmem[2].size = COROUTINE_STACKSIZE;
    stackmem[3].memory = (void*)stack[3];
    stackmem[3].size = COROUTINE_STACKSIZE;
    FiberPool_init(stackmem, num, 1);
}

void co_submit_task(void (*func)(void), void *args, co_handle_t *handle) {
    FiberPool_push(func, args, 2, handle);
}

void co_wakeup(co_handle_t handle) {
    Fiber_wake(handle);
}

bool co_havefreeslot(co_handle_t* index) {
    *index = FiberPool_FindFree();
    return *index != INVALID_COHANDLE;
}

bool co_check_if_finished(co_handle_t handle) {
    return handle == INVALID_COHANDLE;
}

#endif

#ifdef USE_LIBMICROKITCO
#include <stdint.h>

char libmicrokitco_control[LIBMICROKITCO_CONTROLLER_SIZE];
microkit_cothread_sem_t sem[WORKER_COROUTINE_NUM];

void co_init(uint64_t* stack, uint32_t num) {
    microkit_cothread_init((struct cothreads_control*) libmicrokitco_control, 
                            COROUTINE_STACKSIZE, 
                            stack[0], 
                            stack[1], 
                            stack[2], 
                            stack[3]);
    for (uint32_t i = 0; i < WORKER_COROUTINE_NUM; i++) {
        microkit_cothread_semaphore_init(&sem[i]);
    }
}

// This seems to not invalidate the handle when one coroutine finishes
void co_submit_task(void (*func)(void), void *args, co_handle_t *handle) {
    microkit_cothread_spawn(func, args);
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

#endif