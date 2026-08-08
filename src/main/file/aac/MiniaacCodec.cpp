#include <cstdint>

// Miniaac 1.0.0 uses uint32_t directly in its implementation. Unlike
// <stdint.h>, MSVC's <cstdint> is not required to expose that name globally.
using std::uint32_t;

#define MAAC_COMPACT_CODEBOOKS
#define MAAC_IMPLEMENTATION
#include <maac.h>
