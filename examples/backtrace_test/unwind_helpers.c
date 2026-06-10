#include <microkit.h>
#include <sddf/util/printf.h>
#define UNW_LOCAL_ONLY
#include <libunwind.h>

void show_backtrace (void) {
  microkit_dbg_puts("SHOW_BACKTRACE | BEGIN_SHOW_BACKTRACE\n");
  unw_cursor_t cursor; 
  unw_context_t uc;
  unw_word_t ip, sp;

  unw_getcontext(&uc);
  unw_init_local(&cursor, &uc);
  // TODO: print the backtrace depth, and possibly find the culprit function address?
  while (unw_step(&cursor) > 0) {
    unw_get_reg(&cursor, UNW_REG_IP, &ip);
    unw_get_reg(&cursor, UNW_REG_SP, &sp);
    sddf_printf("ip = %lx, sp = %lx\n", (long) ip, (long) sp);
  }
  microkit_dbg_puts("SHOW_BACKTRACE | END_SHOW_BACKTRACE\n");
}
