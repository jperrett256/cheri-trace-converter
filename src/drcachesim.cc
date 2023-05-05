#include <zlib.h>
#include <assert.h>

#include "dynamorio/clients/drcachesim/common/trace_entry.h"

#define EXTERN_C extern "C" {
#define EXTERN_C_END }

EXTERN_C

#include "jdp.h"
#include "trace.h"
#include "common.h"
#include "hashmap.h"
#include "drcachesim.h"

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

    trace_entry_t page_size =
    {
        .type = TRACE_TYPE_MARKER,
        .size = TRACE_MARKER_TYPE_PAGE_SIZE,
        .addr = PAGE_SIZE
    };

    gzwrite(file, &header, sizeof(header));
    gzwrite(file, &thread, sizeof(thread));
    gzwrite(file, &pid, sizeof(pid));
    gzwrite(file, &timestamp, sizeof(timestamp));
    gzwrite(file, &cpuid, sizeof(cpuid));
    gzwrite(file, &page_size, sizeof(page_size));
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

static unsigned short get_drcachesim_type(uint8_t type)
{
    switch (type)
    {
        case CUSTOM_TRACE_TYPE_INSTR:
        {
            return TRACE_TYPE_INSTR;
        } break;
        case CUSTOM_TRACE_TYPE_LOAD:
        case CUSTOM_TRACE_TYPE_CLOAD:
        {
            return TRACE_TYPE_READ;
        } break;
        case CUSTOM_TRACE_TYPE_STORE:
        case CUSTOM_TRACE_TYPE_CSTORE:
        {
            return TRACE_TYPE_WRITE;
        } break;
        default: assert(!"Impossible");
    }

    assert(!"Impossible");
    return -1;
}

static void write_drcachesim_tag_entry(gzFile file, uint8_t type, uint8_t tag)
{
    if (type == CUSTOM_TRACE_TYPE_INSTR) assert(tag == 0);
    if (type == CUSTOM_TRACE_TYPE_STORE) assert(tag == 0);

    if (type != CUSTOM_TRACE_TYPE_LOAD && type != CUSTOM_TRACE_TYPE_INSTR)
    {
        // don't know tag for LOADs, know tag is cleared for instructions

        assert(tag == 0 || tag == 1);

        trace_entry_t tag_entry =
        {
            .type = TRACE_TYPE_MARKER,
            .size = TRACE_MARKER_TYPE_TAG_CHERI,
            .addr = tag
        };

        gzwrite(file, &tag_entry, sizeof(tag_entry));
    }
}

static void write_drcachesim_page_mapping_entries(gzFile file, u64 vaddr, u64 paddr)
{
    trace_entry_t paddr_mapping_entry =
    {
        .type = TRACE_TYPE_MARKER,
        .size = TRACE_MARKER_TYPE_PHYSICAL_ADDRESS,
        .addr = paddr
    };
    trace_entry_t vaddr_mapping_entry =
    {
        .type = TRACE_TYPE_MARKER,
        .size = TRACE_MARKER_TYPE_VIRTUAL_ADDRESS,
        .addr = vaddr
    };

    gzwrite(file, &paddr_mapping_entry, sizeof(paddr_mapping_entry));
    gzwrite(file, &vaddr_mapping_entry, sizeof(vaddr_mapping_entry));
}

void write_drcachesim_trace_entry_vaddr(gzFile file, map_u64 page_table, custom_trace_entry_t custom_entry)
{
    assert(file);

    u64 vaddr = custom_entry.vaddr;
    u64 paddr = custom_entry.paddr;

    u64 phys_page_start;
    if (map_u64_get(page_table, get_page_start(vaddr), &phys_page_start))
    {
        if (check_paddr_valid(paddr))
        {
            if (phys_page_start != get_page_start(paddr))
            {
                map_u64_set(page_table, get_page_start(vaddr), get_page_start(paddr));

                write_drcachesim_page_mapping_entries(file, vaddr, paddr);
            }
        }
    }
    else if (check_paddr_valid(paddr))
    {
        map_u64_set(page_table, get_page_start(vaddr), get_page_start(paddr));

        write_drcachesim_page_mapping_entries(file, vaddr, paddr);
    }
    else
    {
        // TODO skip these instead?
        printf("ERROR: missing paddr for first access to page (vaddr: %lu)\n", vaddr);

        trace_entry_t no_mapping_entry =
        {
            .type = TRACE_TYPE_MARKER,
            .size = TRACE_MARKER_TYPE_PHYSICAL_ADDRESS_NOT_AVAILABLE,
            .addr = vaddr
        };

        gzwrite(file, &no_mapping_entry, sizeof(no_mapping_entry));
    }

    write_drcachesim_tag_entry(file, custom_entry.type, custom_entry.tag);

    trace_entry_t drcachesim_entry;
    drcachesim_entry.type = get_drcachesim_type(custom_entry.type);

    drcachesim_entry.size = custom_entry.size;
    drcachesim_entry.addr = custom_entry.vaddr;

    gzwrite(file, &drcachesim_entry, sizeof(drcachesim_entry));
}

void write_drcachesim_trace_entry_paddr(gzFile file, custom_trace_entry_t custom_entry)
{
    write_drcachesim_tag_entry(file, custom_entry.type, custom_entry.tag);

    trace_entry_t drcachesim_entry;
    drcachesim_entry.type = get_drcachesim_type(custom_entry.type);

    drcachesim_entry.size = custom_entry.size;
    drcachesim_entry.addr = custom_entry.paddr;

    gzwrite(file, &drcachesim_entry, sizeof(drcachesim_entry));
}

EXTERN_C_END
