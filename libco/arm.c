#define LIBCO_C
#include "libco.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

static thread_local unsigned long co_active_buffer[64];
static thread_local cothread_t co_active_handle = 0;
static void (*co_swap)(cothread_t, cothread_t) = 0;

section(text)
static const unsigned long co_swap_function[1024] = {
  0xe8a16ff0,  /* stmia r1!, {r4-r11,sp,lr} */
  0xe8b0aff0,  /* ldmia r0!, {r4-r11,sp,pc} */
  0xe12fff1e,  /* bx lr                     */
};

cothread_t co_active() {
  if(!co_active_handle) co_active_handle = &co_active_buffer;
  return co_active_handle;
}

cothread_t co_derive(void* memory, unsigned int size, void (*entrypoint)(void)) {
  unsigned long* handle;
  if(!co_swap) {
    co_swap = (void (*)(cothread_t, cothread_t))co_swap_function;
  }
  if(!co_active_handle) co_active_handle = &co_active_buffer;

  if(handle = (unsigned long*)memory) {
    unsigned int offset = (size & ~15);
    unsigned long* p = (unsigned long*)((unsigned char*)handle + offset);
    handle[8] = (unsigned long)p;
    handle[9] = (unsigned long)entrypoint;
  }

  return handle;
}

void co_switch(cothread_t handle) {
  cothread_t co_previous_handle = co_active_handle;
  co_swap(co_active_handle = handle, co_previous_handle);
}

#ifdef __cplusplus
}
#endif
