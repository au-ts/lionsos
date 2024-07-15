#include "fatfs_config.h"
#include "co_helper.h"

#ifdef USE_FIBERPOOL
void co_init(struct stack_mem* stack, uint32_t num) {
    FiberPool_init(stack, num, 1);
}

void co_submit_task(void (*func)(void), void *args, co_handle_t *handle) {
    FiberPool_push(func, args, 2, handle);
}

bool co_havefreeslot(int32_t* index) {
    *index = FiberPool_FindFree();
    return *index != INVALID_COHANDLE;
}

int32_t co_getindex(co_handle_t handle) {
    return handle;
}

bool co_check_if_finished(co_handle_t handle) {
    return handle == INVALID_COHANDLE;
}

#endif

#ifdef USE_LIBMICROKITCO
#include <stdint.h>

char libmicrokitco_control[WORKER_COROUTINE_NUM];
void co_init(struct stack_mem* stack, uint32_t num) {
    microkit_cothread_init((uintptr_t) libmicrokitco_control, 
                            Coroutine_STACKSIZE, 
                            Coroutine_STACK_ONE, 
                            Coroutine_STACK_TWO, 
                            Coroutine_STACK_THREE, 
                            Coroutine_STACK_FOUR);
}


#endif