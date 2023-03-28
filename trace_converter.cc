#include "dynamorio/clients/drcachesim/common/trace_entry.h"
#include "tag_tracing.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include <iostream>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

// #include <boost/filesystem.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/filtering_stream.hpp>


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

    uint64_t num_CLOADs_no_tag;
    uint64_t num_CSTOREs_no_tag;
};

void update_stats(stats_t * stats, tag_tracing_entry_t entry)
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
            assert(entry.tag_value == TAG_TRACING_TAG_CLEARED);
        } break;
        case TAG_TRACING_TYPE_CLOAD:
        {
            stats->num_CLOADs++;

            if (!entry.paddr) stats->num_CLOADs_no_paddr++;
            if (entry.tag_value == TAG_TRACING_TAG_UNKNOWN) stats->num_CLOADs_no_tag++;
        } break;
        case TAG_TRACING_TYPE_CSTORE:
        {
            stats->num_CSTOREs++;

            if (!entry.paddr) stats->num_CSTOREs_no_paddr++;
            if (entry.tag_value == TAG_TRACING_TAG_UNKNOWN) stats->num_CSTOREs_no_tag++;
        } break;
        default: assert(!"Impossible.");
    }
}

void print_stats(stats_t * stats)
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
    printf("\n");
    printf("\tCLOADs missing tag information: %lu\n", stats->num_CLOADs_no_tag);
    printf("\tCSTOREs missing tag information: %lu\n", stats->num_CSTOREs_no_tag);
}

const char * get_type_string(uint8_t type)
{
    switch (type)
    {
        case TAG_TRACING_TYPE_INSTR:
            return "INSTR";
        case TAG_TRACING_TYPE_LOAD:
            return "LOAD";
        case TAG_TRACING_TYPE_STORE:
            return "STORE";
        case TAG_TRACING_TYPE_CLOAD:
            return "CLOAD";
        case TAG_TRACING_TYPE_CSTORE:
            return "CSTORE";
        default: assert(!"Impossible.");
    }
}

bool file_exists(char * filename)
{
    FILE * fd = fopen(filename, "rb");
    if (fd) fclose(fd);
    return fd != NULL;
}

void quit(void)
{
    printf("Quitting.\n");
    exit(1);
}

uint64_t get_page_start(uint64_t addr)
{
    return addr & ~((1 << 12) - 1);
}

int main(int argc, char * argv[])
{

    if (argc == 2) /* just reads and prints statistics */
    {
        char * input_filename = argv[1];

        boost::iostreams::filtering_istream input_trace_data;
        input_trace_data.push(boost::iostreams::gzip_decompressor());
        input_trace_data.push(boost::iostreams::file_descriptor_source(input_filename));

        stats_t global_stats = {0};

        while (true)
        {
            tag_tracing_entry_t current_entry = {0};
            input_trace_data.read((char *) &current_entry, sizeof(current_entry));

            if (!input_trace_data)
            {
                static_assert(sizeof(current_entry) <= INT32_MAX, "Integral type chosen for gcount may be inappropriate.");
                int32_t bytes_read = (int32_t) input_trace_data.gcount();
                if (input_trace_data.gcount() != 0)
                {
                    printf("ERROR: only able to read %d bytes???\n", bytes_read);
                }
                break;
            }

            update_stats(&global_stats, current_entry);
        }

        print_stats(&global_stats);

        // NOTE just being explicit, the destructors would probably do this anyway
        boost::iostreams::close(input_trace_data);

    }
    else if (argc == 3) /* for removing drcachesim trace header/footer (mistakenly left in) */
    {
        char * input_filename = argv[1];
        char * output_filename = argv[2];

        if (file_exists(output_filename))
        {
            printf("ERROR: Attempted to overwrite existing file \"%s\".\n", output_filename);
            quit();
        }

        boost::iostreams::filtering_istream input_trace_data;
        boost::iostreams::filtering_ostream output_trace_data;

        input_trace_data.push(boost::iostreams::gzip_decompressor());
        input_trace_data.push(boost::iostreams::file_descriptor_source(input_filename));

        output_trace_data.push(boost::iostreams::gzip_compressor());
        output_trace_data.push(boost::iostreams::file_descriptor_sink(output_filename));

        std::unordered_map<uint64_t, uint64_t> page_table;
        std::unordered_set<uint64_t> dbg_pages_without_mapping;

        stats_t global_stats_before = {0};
        stats_t global_stats_after = {0};

        uint64_t dbg_num_addr_mapping_changes = 0;

        while (true)
        {
            tag_tracing_entry_t current_entry = {0};
            input_trace_data.read((char *) &current_entry, sizeof(current_entry));

            if (!input_trace_data)
            {
                static_assert(sizeof(current_entry) <= INT32_MAX, "Integral type chosen for gcount may be inappropriate.");
                int32_t bytes_read = (int32_t) input_trace_data.gcount();
                if (input_trace_data.gcount() != 0)
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
                        printf("WARNING: page table mapping changed at instruction %lu.\n",
                            global_stats_before.num_instructions);
                        printf(
                            "vaddr: " TARGET_FMT_lx ", old paddr page: " TARGET_FMT_lx
                            ", new paddr page: " TARGET_FMT_lx ", type: %s\n",
                            vaddr, paddr_page, get_page_start(paddr), get_type_string(current_entry.type));
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
                    // printf("vaddr is: " TARGET_FMT_lx ", set paddr to: " TARGET_FMT_lx "\n",
                    //     current_entry.vaddr, current_entry.paddr);
                }
                else
                {
                    // NOTE to see how many pages the entries without valid mappings correspond to
                    dbg_pages_without_mapping.insert(get_page_start(vaddr));
                }
            }

            output_trace_data.write((char *) &current_entry, sizeof(tag_tracing_entry_t));
            update_stats(&global_stats_after, current_entry);
        }

        printf("\n");

        printf("Input:\n");
        print_stats(&global_stats_before);
        printf("\n");

        printf("Output:\n");
        print_stats(&global_stats_after);
        printf("\n");

        printf("Strange mapping changes: %lu\n", dbg_num_addr_mapping_changes);
        printf("Pages without paddr mappings: %lu\n", (uint64_t) dbg_pages_without_mapping.size());

        // NOTE just being explicit, the destructors would probably do this anyway
        boost::iostreams::close(input_trace_data);
        boost::iostreams::close(output_trace_data);
    }
    else
    {
        printf("Usage: %s <input trace> [<output trace>]\n", argv[0]);
        quit();
    }

    // TODO use a map to store virtual page addresses
    // writes change mapping, reads read from mapping
    // should make distinction between:
    //  - reads for which there is no mapping (never written to same page)
    //  - writes without tag info to provide mapping (do we assume that mapping doesn't change over course of program?)

    // NOTE this mapping could be quite large for programs that use a lot of memory
    // An alternative, if we need to do two passes anyway (e.g. for tag patching reasons):
    // it may end up being better to create a map of the entries that need their tag value/paddr patched up on the next pass
    // (might even be able to use one map for it idk)

    // TODO tag patching (fixing missing tag info for CSTOREs using later CLOADs)


    // {
    //     // drcachesim needs a header in the file, so we create it here
    //     trace_entry_t header{ .type = TRACE_TYPE_HEADER,
    //                           .size = 0,
    //                           .addr = TRACE_ENTRY_VERSION };
    //     DynamorioTraceInterceptor::mem_logfile.write((char *)&header,
    //                                                  sizeof(header));

    //     // dub thread and pid as we only have one process one thread for now
    //     trace_entry_t thread{ .type = TRACE_TYPE_THREAD, .size = 4, .addr = 1 };
    //     trace_entry_t pid{ .type = TRACE_TYPE_PID, .size = 4, .addr = 1 };

    //     // dub timestamp and cpuid
    //     trace_entry_t timestamp{ .type = TRACE_TYPE_MARKER,
    //                              .size = TRACE_MARKER_TYPE_TIMESTAMP,
    //                              .addr = 0 };
    //     trace_entry_t cpuid{ .type = TRACE_TYPE_MARKER,
    //                          .size = TRACE_MARKER_TYPE_CPU_ID,
    //                          .addr = 0 };

    //     DynamorioTraceInterceptor::mem_logfile.write((char *)&thread,
    //                                                  sizeof(thread));
    //     DynamorioTraceInterceptor::mem_logfile.write((char *)&pid, sizeof(pid));
    //     DynamorioTraceInterceptor::mem_logfile.write((char *)&timestamp,
    //                                                  sizeof(timestamp));
    //     DynamorioTraceInterceptor::mem_logfile.write((char *)&cpuid,
    //                                                  sizeof(cpuid));


    //     // add footer to tracing file
    //     trace_entry_t footer;
    //     footer.type = TRACE_TYPE_FOOTER;
    //     footer.size = 0;
    //     footer.addr = 0;
    //     DynamorioTraceInterceptor::mem_logfile.write((char *)&footer,
    //                                                  sizeof(footer));
    // }

    return 0;
}
