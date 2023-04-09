#ifndef TRACE_INCLUDE
#define TRACE_INCLUDE

#include <inttypes.h>

#define FMT_ADDR "%016" PRIx64

enum trace_entry_type_t
{
    TAG_TRACING_TYPE_INSTR,
    TAG_TRACING_TYPE_LOAD,
    TAG_TRACING_TYPE_STORE,
    TAG_TRACING_TYPE_CLOAD,
    TAG_TRACING_TYPE_CSTORE,
};

typedef struct trace_entry_t trace_entry_t;
struct trace_entry_t
{
    uint8_t type;
    uint8_t tag; // ignore for LOADs
    uint16_t size;
    uintptr_t vaddr; // only for reconstructing the minority of missing paddrs
    uintptr_t paddr;
};

#endif /* TRACE_INCLUDE */
