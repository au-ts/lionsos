#pragma once
#include <microkit.h>
#include <sddf/util/printf.h>

#define LOG(...) do { sddf_printf("%s | ", microkit_name); sddf_printf(__VA_ARGS__); } while (0)
