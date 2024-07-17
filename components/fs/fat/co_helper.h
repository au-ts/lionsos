#pragma once

#include "fatfs_config.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef USE_FIBERPOOL
#include "FiberPool/FiberPool.h"

typedef co_handle co_handle_t;

#define co_yield() Fiber_yield()
#define co_kill() Fiber_kill()
#define co_get_args() Fiber_GetArgs()
#define co_set_args(handle, data) FiberPool_SetArgs(handle, data)
#define co_block() Fiber_block()
#endif

#ifdef USE_LIBMICROKITCO
#include "../../../dep/libmicrokitco/libmicrokitco.h"
typedef microkit_cothread_ref_t co_handle_t;

#define co_get_args() microkit_cothread_my_arg()
#define co_yield() microkit_cothread_yield()
#define co_set_args(handle, data) microkit_cothread_set_arg(handle, data)
#define co_havefreeslot(ptr) microkit_cothread_free_handle_available(ptr)
#endif

void co_init(uint64_t* stack, uint32_t num);
// The handle memory is provided by the user not coroutine pool, is this the right design?
void co_submit_task(void (*func)(void), void *args, co_handle_t *handle);
void co_set_args(co_handle_t handle, void* data);
void* co_get_args();
void co_block();
void co_yield();
void co_kill();
void co_wakeup(co_handle_t handle);
// All coroutine libraries adopt a threadpool model
// so we can ask if the pool still has free slot and each slot can be mapped to an index
bool co_havefreeslot(int32_t* index);
bool co_check_if_finished(co_handle_t handle);