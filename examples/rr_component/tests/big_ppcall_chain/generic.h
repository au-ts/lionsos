#ifndef generic_h_INCLUDED
#define generic_h_INCLUDED

#include <microkit.h>
#include <sddf/util/printf.h>
#define LOG(...) do { sddf_printf("%s | ", microkit_name); sddf_printf(__VA_ARGS__); } while (0)
#define UNSET_VALUE ((seL4_Word)-1)

const char starter_msg[] = "hello world";
const char ender_msg[] = "i love c++ ;)";
// for each ppcall up the chain we add 1 to the value.
const seL4_Word final_label = 0xfeedbeef;

#endif // generic_h_INCLUDED
