#ifndef COMMON_INCLUDE
#define COMMON_INCLUDE

#include "jdp.h"

#define CAP_SIZE_BITS 128
#define CAP_SIZE_BYTES (CAP_SIZE_BITS/8)

#define MEMORY_SIZE GIGABYTES(2)
#define BASE_PADDR 0x80000000

static inline bool check_paddr_valid(u64 paddr)
{
    return paddr >= BASE_PADDR && paddr < BASE_PADDR + MEMORY_SIZE;
}

#define INDENT4 "    "
#define INDENT8 INDENT4 INDENT4

#endif /* COMMON_INCLUDE */
