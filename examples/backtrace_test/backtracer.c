#include <microkit.h>
#include <sddf/util/printf.h>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

void show_backtrace (void) {
  microkit_dbg_puts("BACKTRACER | BEGIN_SHOW_BACKTRACE\n");
  unw_cursor_t cursor; 
  unw_context_t uc;
  unw_word_t ip, sp;

  unw_getcontext(&uc);
  unw_init_local(&cursor, &uc);
  while (unw_step(&cursor) > 0) {
    unw_get_reg(&cursor, UNW_REG_IP, &ip);
    unw_get_reg(&cursor, UNW_REG_SP, &sp);
    sddf_printf("ip = %lx, sp = %lx\n", (long) ip, (long) sp);
  }
  microkit_dbg_puts("BACKTRACER | END_SHOW_BACKTRACE\n");
}

void increase_stackdepth(int size)
{
    if (size > 0)
        increase_stackdepth(size - 1);
    else show_backtrace();
}

void init() {
    microkit_dbg_puts("BACKTRACER | Backtracer initialised!\n");
    sddf_printf("BACKTRACER | Printf test! %d\n", 10);
    increase_stackdepth(5);
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo,
                microkit_msginfo *reply_msginfo) {

	microkit_dbg_puts("BACKTRACER | Fault received!\n");
    return 0;
}

void notified(microkit_channel ch) {}
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo) {}
