#include <zlib.h>
#include <assert.h>

#include "dynamorio/clients/drcachesim/common/trace_entry.h"

#define EXTERN_C extern "C" {
#define EXTERN_C_END }

EXTERN_C

#include "jdp.h"
#include "common.h"
#include "drcachesim.h"

void write_drcachesim_header(trace_writer_t * writer)
{
    assert(writer);

    // add header and other initial marker entries
    trace_entry_t header = {0};
    header.type = TRACE_TYPE_HEADER;
    header.size = 0;
    header.addr = TRACE_ENTRY_VERSION;

    // NOTE only have one process and one thread at the moment
    trace_entry_t thread = {0};
    thread.type = TRACE_TYPE_THREAD;
    thread.size = 4;
    thread.addr = 1;

    trace_entry_t pid = {0};
    pid.type = TRACE_TYPE_PID;
    pid.size = 4;
    pid.addr = 1;

    trace_entry_t timestamp = {0};
    timestamp.type = TRACE_TYPE_MARKER;
    timestamp.size = TRACE_MARKER_TYPE_TIMESTAMP;
    timestamp.addr = 0;

    trace_entry_t cpuid = {0};
    cpuid.type = TRACE_TYPE_MARKER;
    cpuid.size = TRACE_MARKER_TYPE_CPU_ID;
    cpuid.addr = 0;

    trace_entry_t page_size = {0};
    page_size.type = TRACE_TYPE_MARKER;
    page_size.size = TRACE_MARKER_TYPE_PAGE_SIZE;
    page_size.addr = PAGE_SIZE;

    trace_writer_emit(writer, &header, sizeof(header));
    trace_writer_emit(writer, &thread, sizeof(thread));
    trace_writer_emit(writer, &pid, sizeof(pid));
    trace_writer_emit(writer, &timestamp, sizeof(timestamp));
    trace_writer_emit(writer, &cpuid, sizeof(cpuid));
    trace_writer_emit(writer, &page_size, sizeof(page_size));
}

void write_drcachesim_footer(trace_writer_t * writer)
{
    assert(writer);

    trace_entry_t footer = {0};
    footer.type = TRACE_TYPE_FOOTER;
    footer.size = 0;
    footer.addr = 0;

    trace_writer_emit(writer, &footer, sizeof(footer));
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

static void write_drcachesim_tag_entry(trace_writer_t * writer, uint8_t type, uint8_t tag)
{
    if (type == CUSTOM_TRACE_TYPE_INSTR) assert(tag == 0);
    if (type == CUSTOM_TRACE_TYPE_STORE) assert(tag == 0);

    if (type == CUSTOM_TRACE_TYPE_CLOAD || type == CUSTOM_TRACE_TYPE_CSTORE)
    {
        // don't know tag for LOADs, know tag is cleared for INSTRs/STOREs

        assert(tag == 0 || tag == 1);

        trace_entry_t tag_entry = {0};
        tag_entry.type = TRACE_TYPE_MARKER;
        tag_entry.size = TRACE_MARKER_TYPE_TAG_CHERI;
        tag_entry.addr = tag;

        trace_writer_emit(writer, &tag_entry, sizeof(tag_entry));
    }
}

static void write_drcachesim_page_mapping_entries(trace_writer_t * writer, u64 vaddr, u64 paddr)
{
    trace_entry_t paddr_mapping_entry = {0};
    paddr_mapping_entry.type = TRACE_TYPE_MARKER;
    paddr_mapping_entry.size = TRACE_MARKER_TYPE_PHYSICAL_ADDRESS;
    paddr_mapping_entry.addr = paddr;

    trace_entry_t vaddr_mapping_entry = {0};
    vaddr_mapping_entry.type = TRACE_TYPE_MARKER;
    vaddr_mapping_entry.size = TRACE_MARKER_TYPE_VIRTUAL_ADDRESS;
    vaddr_mapping_entry.addr = vaddr;

    trace_writer_emit(writer, &paddr_mapping_entry, sizeof(paddr_mapping_entry));
    trace_writer_emit(writer, &vaddr_mapping_entry, sizeof(vaddr_mapping_entry));
}

void write_drcachesim_trace_entry_vaddr(trace_writer_t * writer, map_u64 page_table, custom_trace_entry_t custom_entry)
{
    assert(writer);

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

                write_drcachesim_page_mapping_entries(writer, vaddr, paddr);
            }
        }
    }
    else if (check_paddr_valid(paddr))
    {
        map_u64_set(page_table, get_page_start(vaddr), get_page_start(paddr));

        write_drcachesim_page_mapping_entries(writer, vaddr, paddr);
    }
    else
    {
        // TODO skip these instead?
        printf("ERROR: missing paddr for first access to page (vaddr: %lu)\n", vaddr);

        trace_entry_t no_mapping_entry = {0};
        no_mapping_entry.type = TRACE_TYPE_MARKER;
        no_mapping_entry.size = TRACE_MARKER_TYPE_PHYSICAL_ADDRESS_NOT_AVAILABLE;
        no_mapping_entry.addr = vaddr;

        trace_writer_emit(writer, &no_mapping_entry, sizeof(no_mapping_entry));
    }

    write_drcachesim_tag_entry(writer, custom_entry.type, custom_entry.tag);

    trace_entry_t drcachesim_entry;
    drcachesim_entry.type = get_drcachesim_type(custom_entry.type);

    drcachesim_entry.size = custom_entry.size;
    drcachesim_entry.addr = custom_entry.vaddr;

    trace_writer_emit(writer, &drcachesim_entry, sizeof(drcachesim_entry));
}

void write_drcachesim_trace_entry_paddr(trace_writer_t * writer, custom_trace_entry_t custom_entry)
{
    write_drcachesim_tag_entry(writer, custom_entry.type, custom_entry.tag);

    trace_entry_t drcachesim_entry;
    drcachesim_entry.type = get_drcachesim_type(custom_entry.type);

    drcachesim_entry.size = custom_entry.size;
    drcachesim_entry.addr = custom_entry.paddr;

    trace_writer_emit(writer, &drcachesim_entry, sizeof(drcachesim_entry));
}

EXTERN_C_END
