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
    if (argc != 3)
    {
        printf("Usage: %s <input trace> <output trace>\n", argv[0]);
        quit();
    }

    char * input_filename = argv[1];
    char * output_filename = argv[2];

    if (file_exists(output_filename))
    {
        printf("ERROR: Attempted to overwrite existing file \"%s\".", output_filename);
        quit();
    }

    std::ifstream input_trace_file(input_filename, std::ios_base::in | std::ios_base::binary);
    boost::iostreams::filtering_istream input_trace_data; // uncompressed

    input_trace_data.push(boost::iostreams::gzip_decompressor());
    // input_trace_raw.push(boost::iostreams::file_descriptor_source(input_filename)); // TODO this might work?
    input_trace_data.push(input_trace_file);

    // TODO sort output file/data stream
    // std::ofstream output_trace_file(output_filename, std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);

    // TODO fix drcachesim trace header/footer situation

    int64_t dbg_total_entries = 0;
    int64_t dbg_num_missing_tags = 0;
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

        // TODO do processing
        dbg_total_entries++;
        if (current_entry.type == TAG_TRACING_TYPE_CSTORE)
        {
            if (current_entry.tag_value == TAG_TRACING_TAG_UNKNOWN) dbg_num_missing_tags++;
        }
    }

    printf("Total entries: %ld\n", dbg_total_entries);
    printf("Total CSTOREs with unknown tag values: %ld\n", dbg_num_missing_tags);

    // TODO use a map to store virtual page addresses
    // writes change mapping, reads read from mapping
    // should make distinction between:
    //  - reads for which there is no mapping (never written to same page)
    //  - writes without tag info to provide mapping (do we assume that mapping doesn't change over course of program?)

    // NOTE this mapping could be quite large for programs that use a lot of memory
    // An alternative, if we need to do two passes anyway (e.g. for tag patching reasons):
    // it may end up being better to create a map of the entries that need their tag value/paddr patched up on the next pass
    // (might even be able to use one map for it idk)

    boost::iostreams::close(input_trace_data);
    input_trace_file.close();

    return 0;
}
