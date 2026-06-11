#include <microkit.h>
#include <sddf/util/printf.h>
#define UNW_LOCAL_ONLY
#include <libunwind.h>

#define INPUT_CAP 1
static seL4_MessageInfo_t empty_msg = {0};
uintptr_t channel_to_backtrace = 0;

void show_backtrace (void) {
  sddf_printf("SHOW_BACKTRACE | BEGIN_SHOW_BACKTRACE for '%s'\n", microkit_name);
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
  microkit_ppcall(channel_to_backtrace, empty_msg);
  microkit_dbg_puts("You're not supposed to see this\n");
}
