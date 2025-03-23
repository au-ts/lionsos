#pragma once

#define BITS_LT(N)  ((N) - 1u)
#define BITS_LE(N)  (BITS_LT(N) | (N))

#define IPV4_ADDR(a, b, c, d) ((a) | ((b) << 8) | ((c) << 16) | ((uint32_t) (d) << 24))
