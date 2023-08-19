#define LIBCO_C
#include "libco.h"
#include "settings.h"

#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

static thread_local long long co_active_buffer[64];
static thread_local cothread_t co_active_handle = 0;
static void (*co_swap)(cothread_t, cothread_t) = 0;

section(text)
/* ABI: SystemV */
const unsigned char co_swap_function[4096] = {
  0x48, 0x89, 0x26,        /* mov [rsi],rsp    */
  0x48, 0x8b, 0x27,        /* mov rsp,[rdi]    */
  0x58,                    /* pop rax          */
  0x48, 0x89, 0x6e, 0x08,  /* mov [rsi+ 8],rbp */
  0x48, 0x89, 0x5e, 0x10,  /* mov [rsi+16],rbx */
  0x4c, 0x89, 0x66, 0x18,  /* mov [rsi+24],r12 */
  0x4c, 0x89, 0x6e, 0x20,  /* mov [rsi+32],r13 */
  0x4c, 0x89, 0x76, 0x28,  /* mov [rsi+40],r14 */
  0x4c, 0x89, 0x7e, 0x30,  /* mov [rsi+48],r15 */
  0x48, 0x8b, 0x6f, 0x08,  /* mov rbp,[rdi+ 8] */
  0x48, 0x8b, 0x5f, 0x10,  /* mov rbx,[rdi+16] */
  0x4c, 0x8b, 0x67, 0x18,  /* mov r12,[rdi+24] */
  0x4c, 0x8b, 0x6f, 0x20,  /* mov r13,[rdi+32] */
  0x4c, 0x8b, 0x77, 0x28,  /* mov r14,[rdi+40] */
  0x4c, 0x8b, 0x7f, 0x30,  /* mov r15,[rdi+48] */
  0xff, 0xe0,              /* jmp rax          */
};

static void co_entrypoint(cothread_t handle) {
  long long* buffer = (long long*)handle;
  void (*entrypoint)(void) = (void (*)(void))buffer[1];
  entrypoint();
  *(int *)0;  /* Panic if cothread_t entrypoint returns */
}

cothread_t co_active() {
  if(!co_active_handle) co_active_handle = &co_active_buffer;
  return co_active_handle;
}

cothread_t co_derive(void* memory, unsigned int size, void (*entrypoint)(void)) {
  cothread_t handle;
  if(!co_swap) {
    co_swap = (void (*)(cothread_t, cothread_t))co_swap_function;
  }
  if(!co_active_handle) co_active_handle = &co_active_buffer;

  if(handle = (cothread_t)memory) {
    unsigned int offset = (size & ~15) - 32;
    long long *p = (long long*)((char*)handle + offset);  /* seek to top of stack */
    *--p = (long long)0;                                  /* crash if entrypoint returns */
    *--p = (long long)co_entrypoint;
    ((long long*)handle)[0] = (long long)p;               /* stack pointer */
    ((long long*)handle)[1] = (long long)entrypoint;      /* start of function */
  }

  return handle;
}

void co_switch(cothread_t handle) {
  register cothread_t co_previous_handle = co_active_handle;
  co_swap(co_active_handle = handle, co_previous_handle);
}

#ifdef __cplusplus
}
#endif
