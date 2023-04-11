#include "jdp.h"
#include "trace.h"
#include "common.h"
#include "handlers.h"

#include <stdio.h>
#include <zlib.h>

// TODO get rid of C++?
#include <unordered_map>
#include <unordered_set>


#define MEMORY_SIZE GIGABYTES(2)
#define BASE_PADDR 0x80000000


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

    uint64_t num_paddrs_invalid; // NOTE includes missing
};

// TODO use this further down as well
static bool check_paddr_valid(u64 paddr)
{
    return paddr >= BASE_PADDR && paddr < BASE_PADDR + MEMORY_SIZE;
}

static void update_stats(stats_t * stats, trace_entry_t entry)
{
    if (entry.type != TRACE_ENTRY_TYPE_INSTR) stats->num_mem_accesses++;
    if (!check_paddr_valid(entry.paddr)) stats->num_paddrs_invalid++;

    switch (entry.type)
    {
        case TRACE_ENTRY_TYPE_INSTR:
        {
            stats->num_instructions++;

            if (!entry.paddr) stats->num_instructions_no_paddr++;
        } break;
        case TRACE_ENTRY_TYPE_LOAD:
        {
            stats->num_LOADs++;

            if (!entry.paddr) stats->num_LOADs_no_paddr++;
        } break;
        case TRACE_ENTRY_TYPE_STORE:
        {
            stats->num_STOREs++;

            if (!entry.paddr) stats->num_STOREs_no_paddr++;
            assert(entry.tag == 0);
        } break;
        case TRACE_ENTRY_TYPE_CLOAD:
        {
            stats->num_CLOADs++;

            if (!entry.paddr) stats->num_CLOADs_no_paddr++;
            assert(entry.tag == 0 || entry.tag == 1);
        } break;
        case TRACE_ENTRY_TYPE_CSTORE:
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
    printf("\n");
    printf("\tInvalid paddrs: %lu\n", stats->num_paddrs_invalid);
}

// static const char * get_type_string(uint8_t type)
// {
//     switch (type)
//     {
//         case TRACE_ENTRY_TYPE_INSTR:
//             return "INSTR";
//         case TRACE_ENTRY_TYPE_LOAD:
//             return "LOAD";
//         case TRACE_ENTRY_TYPE_STORE:
//             return "STORE";
//         case TRACE_ENTRY_TYPE_CLOAD:
//             return "CLOAD";
//         case TRACE_ENTRY_TYPE_CSTORE:
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


void trace_get_info(COMMAND_HANDLER_ARGS)
{
    (void) arena;

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


void trace_patch_paddrs(COMMAND_HANDLER_ARGS)
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

        if (check_paddr_valid(paddr))
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

void trace_convert(COMMAND_HANDLER_ARGS)
{
    // TODO lz4 stuff
}

void trace_convert_drcachesim(COMMAND_HANDLER_ARGS)
{
    // TODO

    // add headers
    // emit trace entries
    // add footers
}

#define CAP_SIZE_BITS 128
#define CAP_SIZE_BYTES (CAP_SIZE_BITS/8)
#define CACHE_LINE_SIZE 128

void trace_get_initial_tags(COMMAND_HANDLER_ARGS)
{
    if (num_args != 2)
    {
        printf("Usage: %s %s <input trace file> <output bin file>\n", exe_name, cmd_name);
        quit();
    }

    char * input_trace_filename = args[0];
    char * initial_tags_filename = args[1];

    if (file_exists(initial_tags_filename))
    {
        printf("ERROR: Attempted to overwrite existing file \"%s\".\n", initial_tags_filename);
        quit();
    }

    gzFile input_trace_file = gzopen(input_trace_filename, "rb");
    assert(input_trace_file);

    FILE * initial_tags_file = fopen(initial_tags_filename, "wb");
    assert(initial_tags_file);


    i64 tag_table_size = MEMORY_SIZE / CAP_SIZE_BYTES / 8;
    u8 * initial_tag_state = arena_push_array(arena, u8, tag_table_size);
    for (i64 i = 0; i < tag_table_size; i++) initial_tag_state[i] = 0;

    std::unordered_set<u64> initialized_tags; // all keys should be CAP_SIZE_BITS aligned
    std::unordered_set<u64> dbg_modified_tags; // keep track of which tags do not match initial tag state

    std::unordered_set<u64> initialized_tags_INSTRs;
    std::unordered_set<u64> initialized_tags_LOADs;
    std::unordered_set<u64> initialized_tags_STOREs;
    std::unordered_set<u64> initialized_tags_CLOADs;
    std::unordered_set<u64> initialized_tags_CSTOREs;

    i64 dbg_idx = 0;
    i64 dbg_paddrs_missing = 0;
    i64 dbg_paddrs_invalid = 0;

    i64 dbg_assumed_tag_incorrect_INSTRs = 0;
    i64 dbg_assumed_tag_incorrect_LOADs = 0;
    i64 dbg_assumed_tag_incorrect_STOREs = 0;
    i64 dbg_assumed_tag_incorrect_CLOADs = 0;
    i64 dbg_assumed_tag_incorrect_CSTOREs = 0;

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

        if (dbg_idx % 10000)
        {
            printf("%ld entries processed.\n", dbg_idx);
        }

        if (current_entry.paddr < BASE_PADDR || current_entry.paddr >= BASE_PADDR + MEMORY_SIZE)
        {
            if (current_entry.paddr == 0) dbg_paddrs_missing++;
            else dbg_paddrs_invalid++;

            continue;
        }

        u64 start_addr = align_floor_pow_2(current_entry.paddr, CAP_SIZE_BYTES);
        u64 end_addr = align_ceil_pow_2(current_entry.paddr + current_entry.size, CAP_SIZE_BYTES);
        assert(start_addr < end_addr);

        // printf("start_addr: %lu, end_addr: %lu\n", start_addr, end_addr);

        for (u64 paddr = start_addr; paddr < end_addr; paddr += CAP_SIZE_BYTES)
        {
            // if (!(paddr >= BASE_PADDR && paddr < BASE_PADDR + MEMORY_SIZE))
            // {
            //     printf("paddr: " FMT_ADDR ", current_entry.paddr: " FMT_ADDR "\n", paddr, current_entry.paddr);
            //     printf("BASE_PADDR: " FMT_ADDR ", BASE_PADDR + MEMORY_SIZE: " FMT_ADDR "\n", BASE_PADDR, BASE_PADDR + MEMORY_SIZE);
            //     fflush(stdout);
            // }
            assert(paddr >= BASE_PADDR && paddr < BASE_PADDR + MEMORY_SIZE);
            u64 mem_offset = paddr - BASE_PADDR;

            i64 tag_table_idx = mem_offset / CAP_SIZE_BYTES / 8;
            i8 tag_entry_bit = mem_offset / CAP_SIZE_BYTES % 8;
            assert(tag_table_idx >= 0 && tag_table_idx < tag_table_size);

            bool already_found_first_access = initialized_tags.find(paddr) != initialized_tags.end();
            if (!already_found_first_access)
            {
                switch (current_entry.type)
                {
                    case TRACE_ENTRY_TYPE_INSTR:
                    {
                        initialized_tags_INSTRs.insert(paddr);
                        assert((initial_tag_state[tag_table_idx] & (1 << tag_entry_bit)) == 0);
                        // NOTE assuming tag is 0, leaving as initialised
                    } break;
                    case TRACE_ENTRY_TYPE_LOAD:
                    {
                        initialized_tags_LOADs.insert(paddr);
                        assert((initial_tag_state[tag_table_idx] & (1 << tag_entry_bit)) == 0);
                        // NOTE assuming tag is 0, leaving as initialised
                    } break;
                    case TRACE_ENTRY_TYPE_STORE:
                    {
                        initialized_tags_STOREs.insert(paddr);
                        dbg_modified_tags.insert(paddr);
                        assert((initial_tag_state[tag_table_idx] & (1 << tag_entry_bit)) == 0);
                        // NOTE assuming tag is 0, leaving as initialised
                    } break;
                    case TRACE_ENTRY_TYPE_CLOAD:
                    {
                        initialized_tags_CLOADs.insert(paddr);
                        assert(paddr == current_entry.paddr && "Unaligned CLOAD.");
                        if (current_entry.tag) // TODO get rid of branch?
                        {
                            initial_tag_state[tag_table_idx] |= 1 << tag_entry_bit;
                            assert((initial_tag_state[tag_table_idx] & (1 << tag_entry_bit)) != 0);
                        }
                        else assert((initial_tag_state[tag_table_idx] & (1 << tag_entry_bit)) == 0);
                    } break;
                    case TRACE_ENTRY_TYPE_CSTORE:
                    {
                        initialized_tags_CSTOREs.insert(paddr);
                        dbg_modified_tags.insert(paddr);
                        assert(paddr == current_entry.paddr && "Unaligned CSTORE.");
                        // NOTE assuming tag is 1
                        /* TODO does this actually affect caching behaviour?
                         * If you are setting the entire cache line, do you even need to read from memory/cache first?
                         * (can you not just update/create an entry in L1 and be done with it?) *QUESTION*
                         */
                        initial_tag_state[tag_table_idx] |= 1 << tag_entry_bit;
                    } break;
                    default: assert(!"Impossible.");
                }

                initialized_tags.insert(paddr);
            }
            else
            {
                if (current_entry.type == TRACE_ENTRY_TYPE_STORE || current_entry.type == TRACE_ENTRY_TYPE_CSTORE)
                {
                    dbg_modified_tags.insert(paddr);
                }
                else if (current_entry.type == TRACE_ENTRY_TYPE_CLOAD)
                {
                    bool was_modified_since_init = dbg_modified_tags.find(paddr) != dbg_modified_tags.end();
                    // TODO need to check there was no intervening STORE/CSTORE that would make this check invalid
                    if (!was_modified_since_init)
                    {
                        bool recorded_tag_value = (initial_tag_state[tag_table_idx] & (1 << tag_entry_bit)) != 0;
                        if (recorded_tag_value != current_entry.tag)
                        {
                            if (initialized_tags_INSTRs.find(paddr) != initialized_tags_INSTRs.end())
                                dbg_assumed_tag_incorrect_INSTRs++;
                            else if (initialized_tags_LOADs.find(paddr) != initialized_tags_LOADs.end())
                                dbg_assumed_tag_incorrect_LOADs++;
                            else if (initialized_tags_STOREs.find(paddr) != initialized_tags_STOREs.end())
                                dbg_assumed_tag_incorrect_STOREs++;
                            else if (initialized_tags_CLOADs.find(paddr) != initialized_tags_CLOADs.end())
                                dbg_assumed_tag_incorrect_CLOADs++;
                            else if (initialized_tags_CSTOREs.find(paddr) != initialized_tags_CSTOREs.end())
                                dbg_assumed_tag_incorrect_CSTOREs++;
                            else
                                assert(!"Impossible");
                        }
                    }
                }
                // TODO asserts?
                // (with LOADs, maybe check for following CLOADs that say otherwise?)
                // (with STOREs, assert folowing CLOADs read 0 for the tag)
                // (with CSTOREs, assert folowing CLOADs read correct value for the tag)
            }
        }
    }

    printf("Entries missing paddrs (skipped): %ld\n", dbg_paddrs_missing);
    printf("Entries with invalid paddrs (skipped): %ld\n", dbg_paddrs_invalid);
    printf("Number of times tags were incorrect (INSTRs): %ld\n", dbg_assumed_tag_incorrect_INSTRs);
    printf("Number of times tags were incorrect (LOADs): %ld\n", dbg_assumed_tag_incorrect_LOADs);
    printf("Number of times tags were incorrect (STOREs): %ld\n", dbg_assumed_tag_incorrect_STOREs);
    printf("Number of times tags were incorrect (CLOADs): %ld\n", dbg_assumed_tag_incorrect_CLOADs);
    printf("Number of times tags were incorrect (CSTOREs): %ld\n", dbg_assumed_tag_incorrect_CSTOREs);

    gzclose(input_trace_file);

    fwrite(initial_tag_state, 1, tag_table_size, initial_tags_file);
    fclose(initial_tags_file);

    /* TODO
     * create 2 MiB buffer of tags, set to zero, that we'll update over time
     * create set that we'll fill with entries indicating whether or not we already have the initial value of a tag or not
     * LOADs/STOREs - assume it was 0 before
     * (with LOADs, maybe check for following CLOADs that say otherwise?)
     * (with STOREs, assert folowing CLOADs read 0 for the tag)
     * with CLOADs, we have the original tag value
     * with CSTOREs, we don't know, so we can just assume they were 1s
        - Does this actually affect caching behaviour? If you are setting the entire cache line, do you even need to read from memory/cache first?
          (can you not just update/create an entry in L1 and be done with it?) *QUESTION*
     */
}


// // for the L1/L2/L3 caches
// typedef struct cache_line_t cache_line_t;
// struct cache_line_t
// {
//     // TODO fit in 8 bytes? (don't need lowest 12 bits of tag, counter can be 8 bits, can use other 4 bits for tag and any other metadata)
//     int64_t tag;
//     u32 counter;
//     bool tag;
//     bool dirty;
// };

// // for the tag cache
// typedef struct tag_cache_line_t tag_cache_line_t;
// struct tag_cache_line_t
// {
//     int64_t tag;
//     u8 data[TAG_CACHE_GF/8];
//     u32 counter;
//     bool dirty;
// };

// // TODO do we need to store tag data in cache lines or can it all be in a single table?


void trace_simulate(COMMAND_HANDLER_ARGS)
{
    if (num_args != 2)
    {
        printf("Usage: %s %s <trace file> <initial state>\n", exe_name, cmd_name);
        quit();
    }

    char * input_trace_filename = args[0];
    char * initial_tags_filename = args[1];

    // TODO
    (void) input_trace_filename;
    (void) initial_tags_filename;

    // TODO intialise leaf tag table from initial tag state file
    // TODO initialise root tag table from the leaf tag table
    /* TODO do I need the root tag table or can that be implicit?
     *  - Also, when does it get updated? Because root table cache lines are only ever stored in the tag cache,
     *    and they reflect the tag cache's view of memory, right?
     *      - Am I correct in thinking: *QUESTION*
     *          - When a cache line in the LLC (e.g. L2) gets written back, the root table *and* leaf table cache line entries in the tag cache are updated
     *              - TODO I believe we can assume that
     *          - The root table and leaf table entries in physical memory are only updated when the lines are evicted from
     */

    // TODO sizes from paper mentioned: 32KiB L1 caches and a 256KiB L2, 32KiB for tag cache (all 4-way set associative)

    // TODO another use for this simulator is providing "core dumps", snapshots of the memory contents at runtime to see pointer/tag distribution

    /* TODO L1, L2, L3 caches:
     *  - LOADs pull in full cache lines, presumably this includes the corresponding tag? *QUESTION*
     *      - We don't know the tags, but can assume they are 0 and check if later there's a corresponding CLOAD with tag value 1 from the same address
     *          - may involve storing some extra bits in cache entries, but should be fine
     *  - Do we want to simulate multicore cache behaviour by playing traces back as though multiple "CPUs" were executing programs? *QUESTION*
     *      - Can play different traces back at the same time, or the same trace but at an offset or something (otherwise they will be perfectly in sync)
     *  - Can store tags directly in them, along with whatever state is needed to keep track of cache lines
     *  - Just to check:
     *      - With 128 byte cache lines and 128 bit (16 byte) capabilities, we have 8 tag bits stored in each L1/L2/... cache line?
     */

    /* TODO tag cache:
     *  - TODO try without compression first
     *      - This way you can compare against drcachesim runs
     *  - TODO need to understand compression properly
     *      - You have root tag table cache lines in memory, leaf tag table cache lines in memory
     *          - Are these at the end of memory? Leaf and then root? *QUESTION*
     *          - Whatever you guess for now, assert no memory accesses in the trace are accessing the range you pick
     *              - Where does QEMU store the tags? In guest physical memory or no? *QUESTION*
     *      - Algorithm:
     *          - Check corresponding root tag table entry
     *          - If the corresponding bit in corresponding root tag table entry is:
     *              - cleared - you know the tag is unset for this data access
     *              - set - gotta check the corresponding bit in the corresponding leaf tag table entry
     *      - The tag cache just stores root and leaf tag table cache lines together
     *      - Important that the leaf table cache line not be fetched from memory before the corresponding bit in the root table entry is confirmed to be set
     *          - Otherwise you'd get no benefit from the compression, leaf cache lines would take up space even when root entry bit is 0
     *          - Although I am curious if the hardware implementation makes efforts to reduce latency by
     *            e.g. looking up the root and leaf table cache entries at the same time (if that's even possible)? *QUESTION*
     */

    /* TODO memory:
     *  - Obviously only need to store tag information
     *  - Use a hashset to store the set tags that make their way to memory?
     *      - TODO only storing 1s may not work, must remember that starting memory content is unknown
     */

    /* TODO
     *  - It's now occurring to me that maybe we can store all tag data in a single table
            - where that data is in the cache hierarchy can be implicit
                - know from accesses and cache line invalidations when stuff should be moved up or down
                - still need some way of indicating invalid cache lines (like TAG_INVALID in drcachesim)
            - would make it easier to produce "core dumps" (don't have to consolidate information)

     */
}
