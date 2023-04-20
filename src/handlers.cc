#include "jdp.h"
#include "trace.h"
#include "common.h"
#include "handlers.h"
#include "drcachesim.h"

#include <stdio.h>
#include <zlib.h>
#include <inttypes.h>

#define FMT_ADDR "%016" PRIx64

// TODO get rid of C++?
#include <unordered_map>
#include <unordered_set>


#define MEMORY_SIZE GIGABYTES(2)
#define BASE_PADDR 0x80000000

static bool check_paddr_valid(u64 paddr)
{
    return paddr >= BASE_PADDR && paddr < BASE_PADDR + MEMORY_SIZE;
}


typedef struct trace_stats_t trace_stats_t;
struct trace_stats_t
{
    // NOTE invalid paddr counts include missing paddrs

    u64 num_entries;
    u64 num_entries_no_paddr;
    u64 num_entries_invalid_paddr;

    u64 num_entries_paddr_matches_vaddr;
    u64 num_entries_invalid_paddr_matches_vaddr;

    u64 num_instructions;
    u64 num_instructions_no_paddr;
    u64 num_instructions_invalid_paddr;

    u64 num_mem_accesses;

    u64 num_LOADs;
    u64 num_LOADs_no_paddr;
    u64 num_LOADs_invalid_paddr;

    u64 num_STOREs;
    u64 num_STOREs_no_paddr;
    u64 num_STOREs_invalid_paddr;

    u64 num_CLOADs;
    u64 num_CLOADs_no_paddr;
    u64 num_CLOADs_invalid_paddr;

    u64 num_CSTOREs;
    u64 num_CSTOREs_no_paddr;
    u64 num_CSTOREs_invalid_paddr;
};

static void update_trace_stats(trace_stats_t * stats, custom_trace_entry_t entry)
{
    stats->num_entries++;
    if (!entry.paddr) stats->num_entries_no_paddr++;
    if (!check_paddr_valid(entry.paddr)) stats->num_entries_invalid_paddr++;

    if (entry.vaddr == entry.paddr)
    {
        stats->num_entries_paddr_matches_vaddr++;
        assert(entry.paddr);
        if (!check_paddr_valid(entry.paddr)) stats->num_entries_invalid_paddr_matches_vaddr++;
    }

    if (entry.type != CUSTOM_TRACE_TYPE_INSTR) stats->num_mem_accesses++;

    switch (entry.type)
    {
        case CUSTOM_TRACE_TYPE_INSTR:
        {
            stats->num_instructions++;

            if (!entry.paddr) stats->num_instructions_no_paddr++;
            if (!check_paddr_valid(entry.paddr)) stats->num_instructions_invalid_paddr++;
        } break;
        case CUSTOM_TRACE_TYPE_LOAD:
        {
            stats->num_LOADs++;

            if (!entry.paddr) stats->num_LOADs_no_paddr++;
            if (!check_paddr_valid(entry.paddr)) stats->num_LOADs_invalid_paddr++;
        } break;
        case CUSTOM_TRACE_TYPE_STORE:
        {
            stats->num_STOREs++;

            if (!entry.paddr) stats->num_STOREs_no_paddr++;
            if (!check_paddr_valid(entry.paddr)) stats->num_STOREs_invalid_paddr++;
            assert(entry.tag == 0);
        } break;
        case CUSTOM_TRACE_TYPE_CLOAD:
        {
            stats->num_CLOADs++;

            if (!entry.paddr) stats->num_CLOADs_no_paddr++;
            if (!check_paddr_valid(entry.paddr)) stats->num_CLOADs_invalid_paddr++;
            assert(entry.tag == 0 || entry.tag == 1);
        } break;
        case CUSTOM_TRACE_TYPE_CSTORE:
        {
            stats->num_CSTOREs++;

            if (!entry.paddr) stats->num_CSTOREs_no_paddr++;
            if (!check_paddr_valid(entry.paddr)) stats->num_CSTOREs_invalid_paddr++;
            assert(entry.tag == 0 || entry.tag == 1);
        } break;
        default: assert(!"Impossible.");
    }
}

static void print_trace_stats(trace_stats_t * stats)
{
    printf("Statistics:\n");
    printf("\tTotal entries: %lu\n", stats->num_entries);
    printf("\tTotal entries without paddr: %lu\n", stats->num_entries_no_paddr);
    printf("\tTotal entries with invalid paddr: %lu\n", stats->num_entries_invalid_paddr);
    printf("\n");
    printf("\tTotal entries where paddr == vaddr: %ld\n", stats->num_entries_paddr_matches_vaddr);
    printf("\tTotal entries where invalid paddr == vaddr: %ld\n", stats->num_entries_invalid_paddr_matches_vaddr);
    printf("\n");
    printf("\tInstructions: %lu\n", stats->num_instructions);
    printf("\tInstructions without paddr: %lu\n", stats->num_instructions_no_paddr);
    printf("\tInstructions with invalid paddr: %lu\n", stats->num_instructions_invalid_paddr);
    printf("\n");
    printf("\tTotal memory accesses: %lu\n", stats->num_mem_accesses);
    printf("\n");
    printf("\tLOADs: %lu\n", stats->num_LOADs);
    printf("\tLOADs without paddr: %lu\n", stats->num_LOADs_no_paddr);
    printf("\tLOADs with invalid paddr: %lu\n", stats->num_LOADs_invalid_paddr);
    printf("\n");
    printf("\tSTOREs: %lu\n", stats->num_STOREs);
    printf("\tSTOREs without paddr: %lu\n", stats->num_STOREs_no_paddr);
    printf("\tSTOREs with invalid paddr: %lu\n", stats->num_STOREs_invalid_paddr);
    printf("\n");
    printf("\tCLOADs: %lu\n", stats->num_CLOADs);
    printf("\tCLOADs without paddr: %lu\n", stats->num_CLOADs_no_paddr);
    printf("\tCLOADs with invalid paddr: %lu\n", stats->num_CLOADs_invalid_paddr);
    printf("\n");
    printf("\tCSTOREs: %lu\n", stats->num_CSTOREs);
    printf("\tCSTOREs without paddr: %lu\n", stats->num_CSTOREs_no_paddr);
    printf("\tCSTOREs with invalid paddr: %lu\n", stats->num_CSTOREs_invalid_paddr);
}

// static const char * get_type_string(u8 type)
// {
//     switch (type)
//     {
//         case CUSTOM_TRACE_TYPE_INSTR:
//             return "INSTR";
//         case CUSTOM_TRACE_TYPE_LOAD:
//             return "LOAD";
//         case CUSTOM_TRACE_TYPE_STORE:
//             return "STORE";
//         case CUSTOM_TRACE_TYPE_CLOAD:
//             return "CLOAD";
//         case CUSTOM_TRACE_TYPE_CSTORE:
//             return "CSTORE";
//         default: assert(!"Impossible.");
//     }
// }

// static void debug_print_entry(custom_trace_entry_t entry)
// {
//     printf("%s [ vaddr: " FMT_ADDR ", paddr: " FMT_ADDR ", size: %hu ]\n",
//         get_type_string(entry.type), entry.vaddr, entry.paddr, entry.size);
// }

static bool file_exists(char * filename)
{
    FILE * fd = fopen(filename, "rb");
    if (fd) fclose(fd);
    return fd != NULL;
}

static bool gz_at_eof(int bytes_read, int expected_bytes)
{
    if (bytes_read < 0)
    {
        printf("ERROR: error reading gzip file.\n");
        // TODO call gzerror?
        quit();
    }


    if (bytes_read < expected_bytes)
    {
        assert(bytes_read >= 0);
        if (bytes_read != 0)
        {
            printf("ERROR: attempted to read %d bytes, was only able to read %d bytes.\n", expected_bytes, bytes_read);
        }
        return true;
    }

    return false;
}

static u64 get_page_start(u64 addr)
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

    trace_stats_t global_stats = {0};

    while (true)
    {
        custom_trace_entry_t current_entry = {0};
        int bytes_read = gzread(input_trace_file, &current_entry, sizeof(current_entry));

        assert(sizeof(current_entry) <= INT_MAX);
        if (gz_at_eof(bytes_read, sizeof(current_entry))) break;

        update_trace_stats(&global_stats, current_entry);
    }

    print_trace_stats(&global_stats);

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

    std::unordered_map<u64, u64> page_table;
    std::unordered_set<u64> dbg_pages_changed_mapping;
    std::unordered_set<u64> dbg_pages_without_mapping;

    trace_stats_t global_stats_before = {0};
    trace_stats_t global_stats_after = {0};

    u64 dbg_num_addr_mapping_changes = 0;

    // TODO could output compressed traces that omit vaddrs? maybe we should have a header with some metadata in it

    while (true)
    {
        custom_trace_entry_t current_entry = {0};
        int bytes_read = gzread(input_trace_file, &current_entry, sizeof(current_entry));

        assert(sizeof(current_entry) <= INT_MAX);
        if (gz_at_eof(bytes_read, sizeof(current_entry))) break;

        update_trace_stats(&global_stats_before, current_entry);

        u64 vaddr = current_entry.vaddr;
        u64 paddr = current_entry.paddr;
        assert(vaddr);

        if (check_paddr_valid(paddr))
        {
            auto table_entry = page_table.find(get_page_start(vaddr));
            if (table_entry != page_table.end())
            {
                u64 paddr_page = table_entry->second;
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
                u64 paddr_page = table_entry->second;
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
        update_trace_stats(&global_stats_after, current_entry);
    }

    printf("\n");

    printf("Input:\n");
    print_trace_stats(&global_stats_before);
    printf("\n");

    printf("Output:\n");
    print_trace_stats(&global_stats_after);
    printf("\n");

    printf("Mapping changes: %lu\n", dbg_num_addr_mapping_changes);
    printf("Pages with mapping changes: %lu\n", (u64) dbg_pages_changed_mapping.size());
    printf("Pages without paddr mappings: %lu\n", (u64) dbg_pages_without_mapping.size());

    gzclose(input_trace_file);
    gzclose(output_trace_file);
}

void trace_convert(COMMAND_HANDLER_ARGS)
{
    // TODO lz4 stuff
}

void trace_convert_drcachesim(COMMAND_HANDLER_ARGS)
{
    // TODO test

    if (num_args != 2)
    {
        printf("Usage: %s %s <input custom trace file> <output drcachesim trace file>\n", exe_name, cmd_name);
        quit();
    }

    char * input_trace_filename = args[0];
    char * output_trace_filename = args[1];

    if (file_exists(output_trace_filename))
    {
        printf("ERROR: Attempted to overwrite existing file \"%s\".\n", output_trace_filename);
        quit();
    }

    gzFile input_file = gzopen(input_trace_filename, "rb");
    gzFile output_file = gzopen(output_trace_filename, "wb");

    write_drcachesim_header(output_file);

    while (true)
    {
        custom_trace_entry_t current_entry;
        int bytes_read = gzread(input_file, &current_entry, sizeof(current_entry));

        assert(sizeof(current_entry) <= INT_MAX);
        if (gz_at_eof(bytes_read, sizeof(current_entry))) break;

        write_drcachesim_trace_entry(output_file, current_entry);
    }

    write_drcachesim_footer(output_file);

    gzclose(input_file);
    gzclose(output_file);
}

#define CAP_SIZE_BITS 128
#define CAP_SIZE_BYTES (CAP_SIZE_BITS/8)

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

    /* TODO
     * Need to test if the default tag state affects anything (if 0, 1 or random has any affect).
     *  - Worth noting that memory that is not accessed, but within the same page as memory that is, likely has 0 for its tag.
     *    (Since pages are zero-initialised.)
     *      - If you account for this, and having 1s vs 0s still has an effect (due to the coverage of a root table cache line),
     *        that would imply that other programs can affect the caching of this program. Side-channel concerns?
     */

    std::unordered_set<u64> initialized_tags; // all keys should be CAP_SIZE_BITS aligned
    std::unordered_set<u64> dbg_modified_tags; // keep track of which tags do not match initial tag state

    std::unordered_set<u64> initialized_tags_INSTRs;
    std::unordered_set<u64> initialized_tags_LOADs;
    std::unordered_set<u64> initialized_tags_CLOADs;

    i64 dbg_paddrs_missing = 0;
    i64 dbg_paddrs_invalid = 0;

    i64 dbg_assumed_tag_incorrect_INSTRs = 0;
    i64 dbg_assumed_tag_incorrect_LOADs = 0;
    i64 dbg_assumed_tag_incorrect_CLOADs = 0;

    while (true)
    {
        custom_trace_entry_t current_entry = {0};
        int bytes_read = gzread(input_trace_file, &current_entry, sizeof(current_entry));

        assert(sizeof(current_entry) <= INT_MAX);
        if (gz_at_eof(bytes_read, sizeof(current_entry))) break;

        if (!check_paddr_valid(current_entry.paddr))
        {
            if (current_entry.paddr == 0) dbg_paddrs_missing++;
            else dbg_paddrs_invalid++;

            continue;
        }

        assert(current_entry.tag == 0 || current_entry.tag == 1);

        u64 start_addr = align_floor_pow_2(current_entry.paddr, CAP_SIZE_BYTES);
        u64 end_addr = align_ceil_pow_2(current_entry.paddr + current_entry.size, CAP_SIZE_BYTES);
        assert(start_addr < end_addr);

        for (u64 paddr = start_addr; paddr < end_addr; paddr += CAP_SIZE_BYTES)
        {
            // TODO utility function for getting tag table idx?
            assert(check_paddr_valid(paddr));
            u64 mem_offset = paddr - BASE_PADDR;

            i64 tag_table_idx = mem_offset / CAP_SIZE_BYTES / 8;
            i8 tag_entry_bit = mem_offset / CAP_SIZE_BYTES % 8;
            assert(tag_table_idx >= 0 && tag_table_idx < tag_table_size);

            bool already_found_first_access = initialized_tags.find(paddr) != initialized_tags.end();
            if (!already_found_first_access)
            {
                switch (current_entry.type)
                {
                    case CUSTOM_TRACE_TYPE_INSTR:
                    {
                        initialized_tags_INSTRs.insert(paddr);
                        assert((initial_tag_state[tag_table_idx] & (1 << tag_entry_bit)) == 0);
                        // NOTE assuming tag is 0, leaving as initialised
                    } break;
                    case CUSTOM_TRACE_TYPE_LOAD:
                    {
                        initialized_tags_LOADs.insert(paddr);
                        // NOTE assuming tag is 0, leaving as initialised
                        assert((initial_tag_state[tag_table_idx] & (1 << tag_entry_bit)) == 0);
                    } break;
                    case CUSTOM_TRACE_TYPE_STORE:
                    {
                        dbg_modified_tags.insert(paddr);
                        // NOTE assuming tag before store is 0, leaving as initialised
                        assert((initial_tag_state[tag_table_idx] & (1 << tag_entry_bit)) == 0);
                    } break;
                    case CUSTOM_TRACE_TYPE_CLOAD:
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
                    case CUSTOM_TRACE_TYPE_CSTORE:
                    {
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
                if (current_entry.type == CUSTOM_TRACE_TYPE_STORE || current_entry.type == CUSTOM_TRACE_TYPE_CSTORE)
                {
                    dbg_modified_tags.insert(paddr);
                }
                else if (current_entry.type == CUSTOM_TRACE_TYPE_CLOAD)
                {
                    bool was_modified_since_init = dbg_modified_tags.find(paddr) != dbg_modified_tags.end();
                    if (!was_modified_since_init)
                    {
                        bool recorded_tag_value = (initial_tag_state[tag_table_idx] & (1 << tag_entry_bit)) != 0;
                        if (recorded_tag_value != current_entry.tag)
                        {
                            // NOTE the LOADs are the main one I'm worried about (LOADs followed by a CLOAD reading a 1 for the tag)

                            if (initialized_tags_INSTRs.find(paddr) != initialized_tags_INSTRs.end())
                                dbg_assumed_tag_incorrect_INSTRs++;
                            else if (initialized_tags_LOADs.find(paddr) != initialized_tags_LOADs.end())
                                dbg_assumed_tag_incorrect_LOADs++;
                            else if (initialized_tags_CLOADs.find(paddr) != initialized_tags_CLOADs.end())
                                dbg_assumed_tag_incorrect_CLOADs++;
                            else
                                assert(!"Impossible");
                        }
                    }
                }
            }
        }
    }

    printf("Entries missing paddrs (skipped): %ld\n", dbg_paddrs_missing);
    printf("Entries with invalid paddrs (skipped): %ld\n", dbg_paddrs_invalid);
    printf("Number of times tags were incorrect (INSTRs): %ld\n", dbg_assumed_tag_incorrect_INSTRs);
    printf("Number of times tags were incorrect (LOADs): %ld\n", dbg_assumed_tag_incorrect_LOADs);
    printf("Number of times tags were incorrect (CLOADs): %ld\n", dbg_assumed_tag_incorrect_CLOADs);

    gzclose(input_trace_file);

    fwrite(initial_tag_state, 1, tag_table_size, initial_tags_file);
    fclose(initial_tags_file);

    /* TODO
     * create 16 MiB buffer of tags, set to zero, that we'll update over time
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

#define CACHE_LINE_SIZE 128
#define CACHE_LINE_SIZE_BITS 7
static_assert((1 << CACHE_LINE_SIZE_BITS) == CACHE_LINE_SIZE, "Cache line size constants are incorrect.");

#define INVALID_TAG ((u64) -1)

// for the L1/L2/L3 caches
typedef struct cache_line_t cache_line_t;
struct cache_line_t
{
    /* TODO fit in 8 bytes?
     *  - don't need lower bits of tag (corresponding to bits for byte within cache line or set index)
     *      - if associativity is not power of 2, will probably need to keep the set bits around (or at least most of them)
     *  - don't need the higher bits of tag either, not using a full 64 bit address space
     *  - counter can be 4 bits (for up to 16-way associativity), 4 bits for tags, probably need a valid bit
     */
    u64 tag_addr;
    u16 counter;
    b8 tags_cheri; // TODO make 16 bits?
    // bool dirty; // TODO
};

typedef struct cache_t cache_t;
struct cache_t
{
    u32 size;
    u32 num_ways;
    cache_t * parent;
    cache_line_t * entries;
};

typedef struct tag_cache_t tag_cache_t;
struct tag_cache_t
{
    // TODO
    // cache_line_t * entries; // NOTE won't store tag in them
    u32 tags_size;
    u8 * tags; // NOTE tag controller's view of memory
};

enum device_type_t
{
    DEVICE_TYPE_CACHE,
    DEVICE_TYPE_TAG_CACHE // TODO if we do actual tag cache simulation elsewhere, could rename to _INTERFACE?
    // TODO memory device as well?
};

typedef struct device_t device_t;
struct device_t
{
    u8 type;
    device_t * parent;
    union
    {
        cache_t cache;
        tag_cache_t tag_cache;
    };
};

// TODO move simulation stuff into another file?
cache_line_t * cache_lookup(device_t * device, u64 paddr);

// TODO do stats properly
u64 dbg_dram_writes = 0;
u64 dbg_dram_reads = 0;

// TODO do actual tag cache simulation
// TODO alternatively, could output requests to tag cache, and run the tag cache simulation in a separate pass (might massively save time)
// TODO should definitely try lz4

void device_write(device_t * device, u64 paddr, b8 tags_cheri)
{
    switch (device->type)
    {
        case DEVICE_TYPE_CACHE:
        {
            cache_line_t * cache_line = cache_lookup(device, paddr);
            cache_line->tags_cheri = tags_cheri;
            // TODO dirty bit?
        } break;
        case DEVICE_TYPE_TAG_CACHE:
        {
            dbg_dram_writes++;

            assert(check_aligned_pow_2(paddr, CACHE_LINE_SIZE));
            assert(check_aligned_pow_2(paddr, CAP_SIZE_BYTES));

            assert(CACHE_LINE_SIZE / CAP_SIZE_BYTES == 8); // TODO handle other cache line sizes

            assert(check_paddr_valid(paddr));
            u64 mem_offset = paddr - BASE_PADDR;
            i64 tag_table_idx = mem_offset / CAP_SIZE_BYTES / 8;
            assert(tag_table_idx >= 0 && tag_table_idx < device->tag_cache.tags_size);

            device->tag_cache.tags[tag_table_idx] = tags_cheri;
            // // TODO can definitely optimise, no need to copy bit by bit (especially if each cache line has 8 tag bits)
            // for (u64 paddr_cap = paddr; paddr_cap < paddr + CACHE_LINE_SIZE; paddr_cap += CAP_SIZE_BYTES)
            // {
            //     // TODO utility function for getting tag idx and tag bit?
            //     assert(check_paddr_valid(paddr));
            //     u64 mem_offset = paddr - BASE_PADDR;

            //     i64 tag_table_idx = mem_offset / CAP_SIZE_BYTES / 8;
            //     i8 tag_entry_bit = mem_offset / CAP_SIZE_BYTES % 8;
            //     assert(tag_table_idx >= 0 && tag_table_idx < device->tag_cache.tags_size);
            //     assert(tag_entry_bit >= 0 && tag_entry_bit < 8);

            //     // initial_tag_state[tag_table_idx] |= 1 << tag_entry_bit;
            // }
        } break;
        default: assert(!"Impossible.");
    }
}

b8 device_read(device_t * device, u64 paddr)
{
    switch (device->type)
    {
        case DEVICE_TYPE_CACHE:
        {
            cache_line_t * cache_line = cache_lookup(device, paddr);
            return cache_line->tags_cheri;
            // TODO dirty bit?
        } break;
        case DEVICE_TYPE_TAG_CACHE:
        {
            dbg_dram_reads++;

            assert(check_aligned_pow_2(paddr, CACHE_LINE_SIZE));
            assert(check_aligned_pow_2(paddr, CAP_SIZE_BYTES));

            assert(CACHE_LINE_SIZE / CAP_SIZE_BYTES == 8); // TODO handle other cache line sizes

            assert(check_paddr_valid(paddr));
            u64 mem_offset = paddr - BASE_PADDR;
            i64 tag_table_idx = mem_offset / CAP_SIZE_BYTES / 8;
            assert(tag_table_idx >= 0 && tag_table_idx < device->tag_cache.tags_size);

            b8 tags_cheri = device->tag_cache.tags[tag_table_idx];

            return tags_cheri;
        } break;
        default: assert(!"Impossible.");
    }

    assert(!"Impossible.");
    return 0;
}

device_t tag_cache_init(arena_t * arena)
{
    device_t device = {0};
    device.type = DEVICE_TYPE_TAG_CACHE;

    device.tag_cache.tags_size = MEMORY_SIZE / CAP_SIZE_BYTES / 8;
    device.tag_cache.tags = arena_push_array(arena, u8, device.tag_cache.tags_size);

    // TODO

    return device;
}

device_t cache_init(arena_t * arena, u32 size, u32 num_ways, device_t * parent)
{
    device_t device = {0};
    device.type = DEVICE_TYPE_CACHE;
    device.parent = parent;

    device.cache.size = size;
    device.cache.num_ways = num_ways;
    assert(size % num_ways == 0);
    // result.num_sets =
    device.cache.entries = arena_push_array(arena, cache_line_t, size);
    for (i64 i = 0; i < size; i++) device.cache.entries[i].tag_addr = INVALID_TAG;

    return device;
}

cache_line_t * cache_lookup(device_t * device, u64 paddr)
{
    assert(device->type == DEVICE_TYPE_CACHE); // TODO switch statement?

    // TODO check equivalent
    // assert(paddr == align_floor_pow_2(paddr, CACHE_LINE_SIZE));
    assert(check_aligned_pow_2(paddr, CACHE_LINE_SIZE));

    u64 tag_addr = paddr >> CACHE_LINE_SIZE_BITS;
    u32 num_sets = device->cache.size / device->cache.num_ways; // TODO calculate num_sets at init?
    u32 set_start_idx = (tag_addr % num_sets) * device->cache.num_ways;

    assert(tag_addr != INVALID_TAG);

    i64 way = -1;
    for (i64 i = 0; i < device->cache.num_ways; i++)
    {
        if (device->cache.entries[set_start_idx + i].tag_addr == tag_addr)
        {
            assert(way == -1);
            way = i;
        }
    }

    cache_line_t * result = NULL;
    if (way >= 0) /* HIT */
    {
        result = &device->cache.entries[set_start_idx + way];
    }
    else /* MISS */
    {
        // choose a way to fill next
        way = -1;
        u16 largest_counter = 0;
        for (i64 i = 0; i < device->cache.num_ways; i++)
        {
            if (device->cache.entries[set_start_idx + i].tag_addr == INVALID_TAG)
            {
                way = i;
                break;
            }

            u16 current_counter = device->cache.entries[set_start_idx + i].counter;
            if (current_counter >= largest_counter)
            {
                assert(current_counter != largest_counter || largest_counter == 0);
                largest_counter = current_counter;
                way = i;
            }
        }
        assert(way >= 0);

        assert(device->parent != NULL);

        cache_line_t * line_to_replace = &device->cache.entries[set_start_idx + way];
        // evict if necessary
        if (line_to_replace->tag_addr != INVALID_TAG) // TODO dirty bit?
        {
            // TODO should definitely be in next level cache already when writing-back (somehow assert?)
            device_write(device->parent, paddr, line_to_replace->tags_cheri);
        }

        // forward read to next level cache
        b8 tags_cheri = device_read(device->parent, paddr);

        assert(CACHE_LINE_SIZE % CAP_SIZE_BYTES == 0);
        assert(CACHE_LINE_SIZE / CAP_SIZE_BYTES <= 8);
        // assert(CACHE_LINE_SIZE / CAP_SIZE_BYTES == 8 || ((~((1 << (CACHE_LINE_SIZE/CAP_SIZE_BYTES))-1) & tags_cheri) == 0));
        assert((~((1 << (CACHE_LINE_SIZE/CAP_SIZE_BYTES))-1) & tags_cheri) == 0);

        line_to_replace->tag_addr = tag_addr;
        line_to_replace->tags_cheri = tags_cheri;

        result = line_to_replace;


        /* TODO I am assuming I make space for the new data (evicting lines if necessary) before forwarding lookups to the next level cache.

         * This is to avoid cases like:
            1. l1 misses, asks l2
            2. l2 misses, asks memory
            3. l2 evicts e.g. lru cache line to make space for new data (writes back anything if it needs in the process)
            4. l2 gives data to l1
            5. l1 evicts e.g. lru cache line to make space for new data (but this happens to be the same line that l2 just evicted!)
            6. the cache line from l1 that was evicted needs to be written back into the next-level cache!
            7. now l2 needs to fetch the line again, to have it updated??

            TODO presumably the l1 makes space for new cache line before asking l2 for it to avoid this problem
             - writing back would happen before l2 eviction
             - drcachesim source code won't tell you, as they never have to write anything back (no data stored!)
             - look at MESI protocol? idk
             - invariant: when writing back, expect the cache line to be in next-level cache already
         */

    }

    // update cache line counters (lru)
    u16 result_counter = result->counter;

    if (result_counter != 0)
    {
        for (i64 i = 0; i < device->cache.num_ways; i++)
        {
            if (device->cache.entries[set_start_idx + i].counter < result_counter)
            {
                device->cache.entries[set_start_idx + i].counter =
                    (device->cache.entries[set_start_idx + i].counter + 1) % device->cache.num_ways;
            }
        }

        result->counter = 0;
    }


    assert(result != NULL);
    assert(result->counter == 0);

    return result;
}

// // for the tag cache
// typedef struct tag_cache_line_t tag_cache_line_t;
// struct tag_cache_line_t
// {
//     u64 tag;
//     u8 data[TAG_CACHE_GF/8];
//     u32 counter;
//     bool dirty;
// };

// // TODO do we need to store tag data in cache lines or can it all be in a single table?

static inline u8 get_tag_idx(u64 cache_line_addr, u64 cap_addr)
{
    assert(cap_addr >= cache_line_addr);
    u64 offset = cap_addr - cache_line_addr;
    assert(offset < CACHE_LINE_SIZE);
    assert(offset / CAP_SIZE_BYTES <= 8);

    u8 tag_idx = offset / CAP_SIZE_BYTES;

    return tag_idx;
}

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
    gzFile input_trace_file = gzopen(input_trace_filename, "rb");
    FILE * initial_tags_file = fopen(initial_tags_filename, "rb");


    device_t tag_controller = tag_cache_init(arena); // TODO , KILOBYTES(32), 4, NULL);
    size_t bytes_read = fread(tag_controller.tag_cache.tags, sizeof(u8), tag_controller.tag_cache.tags_size, initial_tags_file);
    assert(bytes_read == tag_controller.tag_cache.tags_size);

    device_t l2_cache = cache_init(arena, KILOBYTES(512), 16, &tag_controller);

    device_t l1_instr_cache = cache_init(arena, KILOBYTES(48), 4, &l2_cache);
    device_t l1_data_cache = cache_init(arena, KILOBYTES(32), 4, &l2_cache);

    while (true)
    {
        custom_trace_entry_t current_entry = {0};
        int bytes_read = gzread(input_trace_file, &current_entry, sizeof(current_entry));

        assert(sizeof(current_entry) <= INT_MAX);
        if (gz_at_eof(bytes_read, sizeof(current_entry))) break;

        /* NOTE can just assume all caches are PIPT.
         * VIPT relies on the fact that the lowest bits of the physical and virtual addresses are the same,
         * so this will have no affect on indexing.
         */

        u64 start_addr = align_floor_pow_2(current_entry.paddr, CACHE_LINE_SIZE);
        u64 end_addr = align_ceil_pow_2(current_entry.paddr, CACHE_LINE_SIZE);
        for (u64 paddr = start_addr; paddr < end_addr; paddr += CACHE_LINE_SIZE)
        {
            u64 start_addr_cap = align_floor_pow_2(current_entry.paddr, CAP_SIZE_BYTES);
            u64 end_addr_cap = align_ceil_pow_2(current_entry.paddr, CAP_SIZE_BYTES);

            if (start_addr_cap < paddr) start_addr_cap = paddr;
            if (end_addr_cap > paddr) end_addr_cap = paddr;

            // assert(start_addr_cap_aligned == align_floor_pow_2(start_addr_cap_aligned));
            // assert(end_addr_cap_aligned == align_floor_pow_2(end_addr_cap_aligned));
            assert(check_aligned_pow_2(start_addr_cap, CAP_SIZE_BYTES));
            assert(check_aligned_pow_2(end_addr_cap, CAP_SIZE_BYTES));

            if (current_entry.type == CUSTOM_TRACE_TYPE_INSTR)
            {
                cache_line_t * cache_line = cache_lookup(&l1_instr_cache, paddr);

                assert(current_entry.tag == 0);

                for (u64 paddr_cap = start_addr_cap; paddr_cap < end_addr_cap; paddr_cap += CAP_SIZE_BYTES)
                {
                    u8 tag_idx = get_tag_idx(paddr, paddr_cap);
                    assert((cache_line->tags_cheri & (1 << tag_idx)) == 0);
                }
            }
            else
            {
                cache_line_t * cache_line = cache_lookup(&l1_data_cache, paddr);

                switch (current_entry.type)
                {
                    case CUSTOM_TRACE_TYPE_LOAD: break;
                    case CUSTOM_TRACE_TYPE_CLOAD:
                    {
                        for (u64 paddr_cap = start_addr_cap; paddr_cap < end_addr_cap; paddr_cap += CAP_SIZE_BYTES)
                        {
                            u8 tag_idx = get_tag_idx(paddr, paddr_cap);
                            assert(((cache_line->tags_cheri & (1 << tag_idx)) != 0) == current_entry.tag);
                        }
                    } break;
                    case CUSTOM_TRACE_TYPE_STORE:
                    case CUSTOM_TRACE_TYPE_CSTORE:
                    {
                        assert(current_entry.type != CUSTOM_TRACE_TYPE_STORE || current_entry.tag == 0);
                        for (u64 paddr_cap = start_addr_cap; paddr_cap < end_addr_cap; paddr_cap += CAP_SIZE_BYTES)
                        {
                            u8 tag_idx = get_tag_idx(paddr, paddr_cap);

                            // TODO check this
                            if (current_entry.tag)
                            {
                                cache_line->tags_cheri |= (1 << tag_idx);
                            }
                            else
                            {
                                cache_line->tags_cheri &= ~(1 << tag_idx);
                            }
                        }
                    } break;
                    default: assert(!"Impossible.");
                }
            }
        }
    }

    // TODO do stats properly
    printf("DRAM Reads: %lu\n", dbg_dram_reads);
    printf("DRAM Writes: %lu\n", dbg_dram_writes);

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
     *  - LOADs pull in full cache lines, presumably this includes the corresponding tags? *QUESTION*
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
