#include "fatfs_config.h"
#include <stdbool.h>

#ifdef USE_FIBERPOOL
#include "FiberPool/FiberPool.h"

typedef co_handle co_handle_t;

#define co_yield() Fiber_yield()
#define co_kill() Fiber_kill()
#define co_get_args() Fiber_GetArgs()
#define co_wakeup(handle) Fiber_wake(handle)
#define co_set_args(handle, data) FiberPool_SetArgs(handle, data)
#define co_block() Fiber_block()
#endif

#ifdef USE_LIBMICROKITCO
typedef {
    
} co_handle_t;
#endif

void co_init(struct stack_mem* stack, uint32_t num);
// The handle memory is provided by the user not coroutine pool, is this the right design?
void co_submit_task(void (*func)(void), void *args, co_handle_t *handle);
void co_set_args(co_handle_t handle, void* data);
void* co_get_args();
void co_block();
void co_yield();
void co_kill();
int32_t co_wakeup(co_handle_t handle);
// All coroutine libraries adopt a threadpool model
// so we can ask if the pool still has free slot and each slot can be mapped to an index
bool co_havefreeslot(int32_t* index);
bool co_check_if_finished(co_handle_t handle);