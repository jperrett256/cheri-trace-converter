#ifndef TRACE_INCLUDE
#define TRACE_INCLUDE

#include <stdint.h>

enum custom_trace_type_t
{
    CUSTOM_TRACE_TYPE_INSTR,
    CUSTOM_TRACE_TYPE_LOAD,
    CUSTOM_TRACE_TYPE_STORE,
    CUSTOM_TRACE_TYPE_CLOAD,
    CUSTOM_TRACE_TYPE_CSTORE,
};

typedef struct custom_trace_entry_t custom_trace_entry_t;
struct custom_trace_entry_t
{
    uint8_t type;
    uint8_t tag; // ignore for LOADs
    uint16_t size;
    uintptr_t vaddr; // only for reconstructing the minority of missing paddrs
    uintptr_t paddr;
};

#endif /* TRACE_INCLUDE */
