#include <microkit.h>
#include <stdint.h>

void print_string(const char* str) {
  while (*str) {
    microkit_dbg_putc(*str++);
  }
}

void init(void) {
  print_string("\n\r===========================================\n\r");
  print_string("      LionsOS Graphics Testing Sandbox       \n\r");
  print_string("===========================================\n\r");
  print_string("[SUCCESS] Minimal LionsOS core loaded safely.\n\r");
  print_string("[READY] Sandbox is active. Ready for Vulkan / VirtIO!\n\r");
}

void notified(microkit_channel ch) {
  // Left blank for handling hardware event loops later
}