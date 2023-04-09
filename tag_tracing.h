#pragma once

#include <stdio.h>
#include <inttypes.h>

#ifdef __cplusplus
#define EXTERN_C extern "C" {
#define EXTERN_C_END }
#else
#define EXTERN_C
#define EXTERN_C_END
#endif

#define FMT_ADDR "%016" PRIx64

// #define TAG_TRACING_DBG_LOG

enum tag_tracing_tag_value_t
{
    TAG_TRACING_TAG_CLEARED,
    TAG_TRACING_TAG_SET,
    TAG_TRACING_TAG_UNKNOWN
};

#ifdef __cplusplus
static_assert(TAG_TRACING_TAG_CLEARED == 0, "cleared tag should be 0");
static_assert(TAG_TRACING_TAG_SET == 1, "set tag should be 1");
#endif

enum tag_tracing_type_t
{
    TAG_TRACING_TYPE_INSTR,
    TAG_TRACING_TYPE_LOAD,
    TAG_TRACING_TYPE_STORE,
    TAG_TRACING_TYPE_CLOAD,
    TAG_TRACING_TYPE_CSTORE,
};

typedef struct tag_tracing_entry_t tag_tracing_entry_t;
struct tag_tracing_entry_t
{
    uint8_t type;
    uint8_t tag; // ignore for LOADs
    uint16_t size;
    uintptr_t vaddr; // only for reconstructing the minority of missing paddrs
    uintptr_t paddr;
};
