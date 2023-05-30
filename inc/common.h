#ifndef COMMON_INCLUDE
#define COMMON_INCLUDE

#include "jdp.h"

#define CAP_SIZE_BITS   128
#define CAP_SIZE_BYTES  (CAP_SIZE_BITS/8)

#define MEMORY_SIZE     GIGABYTES(2)
#define BASE_PADDR      0x80000000

static inline bool check_paddr_valid(u64 paddr)
{
    return paddr >= BASE_PADDR && paddr < BASE_PADDR + MEMORY_SIZE;
}

#define PAGE_SIZE       KILOBYTES(4)
#define PAGE_SIZE_BITS  12
static_assert(PAGE_SIZE == (1 << PAGE_SIZE_BITS), "Page size constants do not match.");

static inline u64 get_page_start(u64 addr)
{
    return addr & ~((1 << PAGE_SIZE_BITS) - 1);
}

#define FMT_ADDR "%016" PRIx64

#endif /* COMMON_INCLUDE */
