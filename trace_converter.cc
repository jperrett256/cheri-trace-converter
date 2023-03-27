#include "dynamorio/clients/drcachesim/common/trace_entry.h"
#include "tag_tracing.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include <iostream>
#include <fstream>

// #include <boost/filesystem.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/filtering_stream.hpp>


typedef struct stats_t stats_t;
struct stats_t
{
    uint64_t num_total;

    uint64_t num_LOADs;
    uint64_t num_STOREs;
    uint64_t num_CLOADs;
    uint64_t num_CSTOREs;

    uint64_t num_STOREs_missing_cap_info;
    uint64_t num_CLOADs_missing_cap_info;
    uint64_t num_CSTOREs_missing_cap_info;
};

stats_t dbg_stats = {0};

void update_stats(tag_tracing_entry_t entry)
{
    dbg_stats.num_total++;

    switch (entry.type)
    {
        case TAG_TRACING_TYPE_LOAD:
        {
            dbg_stats.num_LOADs++;
        } break;
        case TAG_TRACING_TYPE_STORE:
        {
            dbg_stats.num_STOREs++;

            if (entry.paddr == 0) dbg_stats.num_STOREs_missing_cap_info++;
            assert(entry.tag_value != TAG_TRACING_TAG_UNKNOWN);
        } break;
        case TAG_TRACING_TYPE_CLOAD:
        {
            dbg_stats.num_CLOADs++;

            if (entry.paddr == 0)
            {
                // TODO check tag value for custom_traces_001?
                // assert(entry.tag_value == TAG_TRACING_TAG_UNKNOWN);
                dbg_stats.num_CLOADs_missing_cap_info++;
            }
        } break;
        case TAG_TRACING_TYPE_CSTORE:
        {
            dbg_stats.num_CSTOREs++;

            if (entry.paddr == 0)
            {
                assert(entry.tag_value == TAG_TRACING_TAG_UNKNOWN);
                dbg_stats.num_CSTOREs_missing_cap_info++;
            }
        } break;
        default: assert(entry.type == TAG_TRACING_TYPE_INSTR);
    }
}

void print_stats(void)
{
    printf("Statistics:\n");
    printf("\tTotal: %lu\n", dbg_stats.num_total);
    printf("\tLOADs: %lu\n", dbg_stats.num_LOADs);
    printf("\tSTOREs: %lu\n", dbg_stats.num_STOREs);
    printf("\tCLOADs: %lu\n", dbg_stats.num_CLOADs);
    printf("\tCSTOREs: %lu\n", dbg_stats.num_CSTOREs);
    printf("\tSTOREs missing capability information: %lu\n", dbg_stats.num_STOREs_missing_cap_info);
    printf("\tCLOADs missing capability information: %lu\n", dbg_stats.num_CLOADs_missing_cap_info);
    printf("\tCSTOREs missing capability information: %lu\n", dbg_stats.num_CSTOREs_missing_cap_info);
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

int main(int argc, char * argv[])
{
    if (argc == 2) /* just reads and prints statistics */
    {
        char * input_filename = argv[1];

        boost::iostreams::filtering_istream input_trace_data;
        input_trace_data.push(boost::iostreams::gzip_decompressor());
        input_trace_data.push(boost::iostreams::file_descriptor_source(input_filename));

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

            update_stats(current_entry);
        }

        print_stats();

        // NOTE just being explicit, the destructors would probably do this anyway
        boost::iostreams::close(input_trace_data);

    }
    else if (argc == 4) /* for removing drcachesim trace header/footer (mistakenly left in) */
    {
        char * input_filename = argv[1];
        char * output_filename = argv[2];

        unsigned long long num_entries_ull = strtoull(argv[3], NULL, 10);
        assert(num_entries_ull <= INT64_MAX);
        int64_t num_entries = (int64_t) num_entries_ull;

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

        /* CONSUME HEADER */
        {
            trace_entry_t drcachesim_entry;
            input_trace_data.read((char *) &drcachesim_entry, sizeof(trace_entry_t)); // header
            assert(input_trace_data);
            assert(drcachesim_entry.type == TRACE_TYPE_HEADER);
            input_trace_data.read((char *) &drcachesim_entry, sizeof(trace_entry_t)); // thread
            assert(input_trace_data);
            assert(drcachesim_entry.type == TRACE_TYPE_THREAD);
            input_trace_data.read((char *) &drcachesim_entry, sizeof(trace_entry_t)); // pid
            assert(input_trace_data);
            assert(drcachesim_entry.type == TRACE_TYPE_PID);
            input_trace_data.read((char *) &drcachesim_entry, sizeof(trace_entry_t)); // timestamp
            assert(input_trace_data);
            assert(drcachesim_entry.type == TRACE_TYPE_MARKER);
            assert(drcachesim_entry.size == TRACE_MARKER_TYPE_TIMESTAMP);
            input_trace_data.read((char *) &drcachesim_entry, sizeof(trace_entry_t)); // cpuid
            assert(input_trace_data);
            assert(drcachesim_entry.type == TRACE_TYPE_MARKER);
            assert(drcachesim_entry.size == TRACE_MARKER_TYPE_CPU_ID);
        }

        for (int64_t i = 0; i < num_entries; i++)
        {
            tag_tracing_entry_t current_entry = {0};
            input_trace_data.read((char *) &current_entry, sizeof(tag_tracing_entry_t));

            assert(input_trace_data);

            output_trace_data.write((char *) &current_entry, sizeof(tag_tracing_entry_t));

            update_stats(current_entry);
        }

        /* CONSUME FOOTER */
        {
            trace_entry_t drcachesim_entry;
            input_trace_data.read((char *) &drcachesim_entry, sizeof(trace_entry_t)); // footer
            assert(input_trace_data);
            assert(drcachesim_entry.type == TRACE_TYPE_FOOTER);
        }

        /* CHECK AT END */
        {
            char dbg_buf[sizeof(tag_tracing_entry_t) * 4];
            input_trace_data.read((char *) &dbg_buf, sizeof(dbg_buf));
            assert(!input_trace_data);
            assert(input_trace_data.gcount() == 0);
        }

        print_stats();

        // NOTE just being explicit, the destructors would probably do this anyway
        boost::iostreams::close(input_trace_data);
        boost::iostreams::close(output_trace_data);
    }
    else
    {
        printf("Usage: %s <input trace> [<output trace> <num entries>]\n", argv[0]);
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
