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
    uint8_t tag; // ignore for LOADs, either 0 or 1
    uint16_t size;
    uint64_t vaddr; // only for reconstructing the minority of missing paddrs
    uint64_t paddr;
};

enum tag_cache_request_type_t
{
    TAG_CACHE_REQUEST_TYPE_READ,
    TAG_CACHE_REQUEST_TYPE_WRITE
};

typedef struct tag_cache_request_t tag_cache_request_t;
struct tag_cache_request_t
{
    uint8_t type;
    uint16_t size; // just the cache line size
    uint16_t tags; // number of tags depends on cache line size
    uint16_t tags_known; // mask indicating which tags are actually known
    uint64_t addr;
};

#endif /* TRACE_INCLUDE */
