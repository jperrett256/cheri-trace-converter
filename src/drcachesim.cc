#include <zlib.h>
#include <assert.h>

#include "trace.h"
#include "dynamorio/clients/drcachesim/common/trace_entry.h"

void write_drcachesim_header(gzFile file)
{
    assert(file);

    // add header and other initial marker entries
    trace_entry_t header =
    {
        .type = TRACE_TYPE_HEADER,
        .size = 0,
        .addr = TRACE_ENTRY_VERSION
    };

    // NOTE only have one process and one thread at the moment
    trace_entry_t thread = { .type = TRACE_TYPE_THREAD, .size = 4, .addr = 1 };
    trace_entry_t pid = { .type = TRACE_TYPE_PID, .size = 4, .addr = 1 };
    trace_entry_t timestamp =
    {
        .type = TRACE_TYPE_MARKER,
        .size = TRACE_MARKER_TYPE_TIMESTAMP,
        .addr = 0
    };
    trace_entry_t cpuid =
    {
        .type = TRACE_TYPE_MARKER,
        .size = TRACE_MARKER_TYPE_CPU_ID,
        .addr = 0
    };

    gzwrite(file, &header, sizeof(header));
    gzwrite(file, &thread, sizeof(thread));
    gzwrite(file, &pid, sizeof(pid));
    gzwrite(file, &timestamp, sizeof(timestamp));
    gzwrite(file, &cpuid, sizeof(cpuid));
}

void write_drcachesim_trace_entry(gzFile file, custom_trace_entry_t custom_entry)
{
    assert(file);

    trace_entry_t drcachesim_entry;
    switch (custom_entry.type)
    {
        case CUSTOM_TRACE_TYPE_LOAD:
        case CUSTOM_TRACE_TYPE_CLOAD:
        {
            drcachesim_entry.type = TRACE_TYPE_READ;
        } break;
        case CUSTOM_TRACE_TYPE_STORE:
        case CUSTOM_TRACE_TYPE_CSTORE:
        {
            drcachesim_entry.type = TRACE_TYPE_WRITE;
        } break;
        default: assert(!"Impossible");
    }

    // TODO should emit vaddr or paddr? paddr for data, vaddr for instructions?
    // TODO test both
    drcachesim_entry.addr = custom_entry.vaddr;
    drcachesim_entry.size = custom_entry.size;

    gzwrite(file, &drcachesim_entry, sizeof(drcachesim_entry));
}

void write_drcachesim_footer(gzFile file)
{
    assert(file);

    trace_entry_t footer =
    {
        .type = TRACE_TYPE_FOOTER,
        .size = 0,
        .addr = 0
    };

    gzwrite(file, &footer, sizeof(footer));
}
