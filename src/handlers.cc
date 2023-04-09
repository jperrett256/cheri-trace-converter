#include "jdp.h"
#include "trace.h"
#include "common.h"
#include "handlers.h"

#include <stdio.h>
#include <zlib.h>

// TODO get rid of C++?
#include <unordered_map>
#include <unordered_set>

typedef struct stats_t stats_t;
struct stats_t
{
    uint64_t num_instructions;
    uint64_t num_instructions_no_paddr;

    uint64_t num_mem_accesses;

    uint64_t num_LOADs;
    uint64_t num_STOREs;
    uint64_t num_CLOADs;
    uint64_t num_CSTOREs;

    uint64_t num_LOADs_no_paddr;
    uint64_t num_STOREs_no_paddr;
    uint64_t num_CLOADs_no_paddr;
    uint64_t num_CSTOREs_no_paddr;
};

static void update_stats(stats_t * stats, trace_entry_t entry)
{
    if (entry.type != TAG_TRACING_TYPE_INSTR) stats->num_mem_accesses++;

    switch (entry.type)
    {
        case TAG_TRACING_TYPE_INSTR:
        {
            stats->num_instructions++;

            if (!entry.paddr) stats->num_instructions_no_paddr++;
        } break;
        case TAG_TRACING_TYPE_LOAD:
        {
            stats->num_LOADs++;

            if (!entry.paddr) stats->num_LOADs_no_paddr++;
        } break;
        case TAG_TRACING_TYPE_STORE:
        {
            stats->num_STOREs++;

            if (!entry.paddr) stats->num_STOREs_no_paddr++;
            assert(entry.tag == 0);
        } break;
        case TAG_TRACING_TYPE_CLOAD:
        {
            stats->num_CLOADs++;

            if (!entry.paddr) stats->num_CLOADs_no_paddr++;
            assert(entry.tag == 0 || entry.tag == 1);
        } break;
        case TAG_TRACING_TYPE_CSTORE:
        {
            stats->num_CSTOREs++;

            if (!entry.paddr) stats->num_CSTOREs_no_paddr++;
            assert(entry.tag == 0 || entry.tag == 1);
        } break;
        default: assert(!"Impossible.");
    }
}

static void print_stats(stats_t * stats)
{
    printf("Statistics:\n");
    printf("\tInstructions: %lu\n", stats->num_instructions);
    printf("\tInstructions without paddr: %lu\n", stats->num_instructions_no_paddr);
    printf("\tTotal memory accesses: %lu\n", stats->num_mem_accesses);
    printf("\n");
    printf("\tLOADs: %lu\n", stats->num_LOADs);
    printf("\tLOADs without paddr: %lu\n", stats->num_LOADs_no_paddr);
    printf("\tSTOREs: %lu\n", stats->num_STOREs);
    printf("\tSTOREs without paddr: %lu\n", stats->num_STOREs_no_paddr);
    printf("\tCLOADs: %lu\n", stats->num_CLOADs);
    printf("\tCLOADs without paddr: %lu\n", stats->num_CLOADs_no_paddr);
    printf("\tCSTOREs: %lu\n", stats->num_CSTOREs);
    printf("\tCSTOREs without paddr: %lu\n", stats->num_CSTOREs_no_paddr);
}

// static const char * get_type_string(uint8_t type)
// {
//     switch (type)
//     {
//         case TAG_TRACING_TYPE_INSTR:
//             return "INSTR";
//         case TAG_TRACING_TYPE_LOAD:
//             return "LOAD";
//         case TAG_TRACING_TYPE_STORE:
//             return "STORE";
//         case TAG_TRACING_TYPE_CLOAD:
//             return "CLOAD";
//         case TAG_TRACING_TYPE_CSTORE:
//             return "CSTORE";
//         default: assert(!"Impossible.");
//     }
// }

static bool file_exists(char * filename)
{
    FILE * fd = fopen(filename, "rb");
    if (fd) fclose(fd);
    return fd != NULL;
}

static uint64_t get_page_start(uint64_t addr)
{
    return addr & ~((1 << 12) - 1);
}


void tracesim_get_info(COMMAND_HANDLER_ARGS)
{
    (void) arena; // TODO

    if (num_args != 1)
    {
        printf("Usage: %s %s <trace file>\n", exe_name, cmd_name);
        quit();
    }

    char * input_filename = args[0];

    gzFile input_trace_file = gzopen(input_filename, "rb");
    assert(input_trace_file);

    // TODO tune buffer size with gzbuffer? manual buffering?

    stats_t global_stats = {0};

    while (true)
    {
        trace_entry_t current_entry = {0};
        int32_t bytes_read = gzread(input_trace_file, &current_entry, sizeof(current_entry));

        static_assert(sizeof(current_entry) <= INT32_MAX, "Integral type chosen may be inappropriate.");
        if (bytes_read < (int32_t) sizeof(current_entry))
        {
            assert(bytes_read >= 0); // TODO call gzerror?
            if (bytes_read != 0)
            {
                printf("ERROR: only able to read %d bytes???\n", bytes_read);
            }
            break;
        }

        update_stats(&global_stats, current_entry);
    }

    print_stats(&global_stats);

    gzclose(input_trace_file);
}


void tracesim_patch_paddrs(COMMAND_HANDLER_ARGS)
{
    if (num_args != 2)
    {
        printf("Usage: %s %s <input trace file> <output trace file>\n", exe_name, cmd_name);
        quit();
    }

    char * input_filename = args[0];
    char * output_filename = args[1];

    if (file_exists(output_filename))
    {
        printf("ERROR: Attempted to overwrite existing file \"%s\".\n", output_filename);
        quit();
    }

    gzFile input_trace_file = gzopen(input_filename, "rb");
    assert(input_trace_file);

    gzFile output_trace_file = gzopen(output_filename, "wb");
    assert(output_trace_file);

    std::unordered_map<uint64_t, uint64_t> page_table;
    std::unordered_set<uint64_t> dbg_pages_changed_mapping;
    std::unordered_set<uint64_t> dbg_pages_without_mapping;

    stats_t global_stats_before = {0};
    stats_t global_stats_after = {0};

    uint64_t dbg_num_addr_mapping_changes = 0;

    while (true)
    {
        trace_entry_t current_entry = {0};
        int32_t bytes_read = gzread(input_trace_file, &current_entry, sizeof(current_entry));

        static_assert(sizeof(current_entry) <= INT32_MAX, "Integral type chosen may be inappropriate.");
        if (bytes_read < (int32_t) sizeof(current_entry))
        {
            assert(bytes_read >= 0); // TODO call gzerror?
            if (bytes_read != 0)
            {
                printf("ERROR: only able to read %d bytes???\n", bytes_read);
            }
            break;
        }

        update_stats(&global_stats_before, current_entry);

        uint64_t vaddr = current_entry.vaddr;
        uint64_t paddr = current_entry.paddr;
        assert(vaddr);

        if (paddr)
        {
            auto table_entry = page_table.find(get_page_start(vaddr));
            if (table_entry != page_table.end())
            {
                uint64_t paddr_page = table_entry->second;
                // TODO why is virtual-physical mapping changing during execution (userspace traces)?
                if (paddr_page != get_page_start(paddr))
                {
                    dbg_num_addr_mapping_changes++;
                    dbg_pages_changed_mapping.insert(get_page_start(vaddr));

                    // TODO seems that some instruction pages are being swapped in and out repeatedly
                    // printf("WARNING: page table mapping changed at instruction %lu.\n",
                    //     global_stats_before.num_instructions);
                    // printf(
                    //     "vaddr: " FMT_ADDR ", old paddr page: " FMT_ADDR
                    //     ", new paddr page: " FMT_ADDR ", type: %s\n",
                    //     vaddr, paddr_page, get_page_start(paddr), get_type_string(current_entry.type));
                }
                // assert(paddr_page == get_page_start(paddr));
            }

            page_table[get_page_start(vaddr)] = get_page_start(paddr);
        }
        else
        {
            auto table_entry = page_table.find(get_page_start(vaddr));
            if (table_entry != page_table.end())
            {
                uint64_t paddr_page = table_entry->second;
                assert(paddr_page);

                current_entry.paddr = paddr_page + (vaddr - get_page_start(vaddr));
                // printf("vaddr is: " FMT_ADDR ", set paddr to: " FMT_ADDR "\n",
                //     current_entry.vaddr, current_entry.paddr);
            }
            else
            {
                // NOTE to see how many pages the entries without valid mappings correspond to
                dbg_pages_without_mapping.insert(get_page_start(vaddr));
            }
        }

        gzwrite(output_trace_file, &current_entry, sizeof(current_entry));
        update_stats(&global_stats_after, current_entry);
    }

    printf("\n");

    printf("Input:\n");
    print_stats(&global_stats_before);
    printf("\n");

    printf("Output:\n");
    print_stats(&global_stats_after);
    printf("\n");

    printf("Mapping changes: %lu\n", dbg_num_addr_mapping_changes);
    printf("Pages with mapping changes: %lu\n", (uint64_t) dbg_pages_changed_mapping.size());
    printf("Pages without paddr mappings: %lu\n", (uint64_t) dbg_pages_without_mapping.size());

    gzclose(input_trace_file);
    gzclose(output_trace_file);
}
