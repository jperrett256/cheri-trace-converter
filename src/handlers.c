#include "jdp.h"
#include "trace.h"
#include "utils.h"
#include "common.h"
#include "handlers.h"
#include "hashmap.h"
#include "drcachesim.h"
#include "simulator.h"
#include "io.h"

#include <stdio.h>
#include <zlib.h>
#include <inttypes.h>


// static char * get_type_string(u8 type)
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
    printf(INDENT4 "Total entries: %lu\n", stats->num_entries);
    printf(INDENT4 "Total entries without paddr: %lu\n", stats->num_entries_no_paddr);
    printf(INDENT4 "Total entries with invalid paddr: %lu\n", stats->num_entries_invalid_paddr);
    printf("\n");
    printf(INDENT4 "Total entries where paddr == vaddr: %ld\n", stats->num_entries_paddr_matches_vaddr);
    printf(INDENT4 "Total entries where invalid paddr == vaddr: %ld\n", stats->num_entries_invalid_paddr_matches_vaddr);
    printf("\n");
    printf(INDENT4 "Instructions: %lu\n", stats->num_instructions);
    printf(INDENT4 "Instructions without paddr: %lu\n", stats->num_instructions_no_paddr);
    printf(INDENT4 "Instructions with invalid paddr: %lu\n", stats->num_instructions_invalid_paddr);
    printf("\n");
    printf(INDENT4 "Total memory accesses: %lu\n", stats->num_mem_accesses);
    printf("\n");
    printf(INDENT4 "LOADs: %lu\n", stats->num_LOADs);
    printf(INDENT4 "LOADs without paddr: %lu\n", stats->num_LOADs_no_paddr);
    printf(INDENT4 "LOADs with invalid paddr: %lu\n", stats->num_LOADs_invalid_paddr);
    printf("\n");
    printf(INDENT4 "STOREs: %lu\n", stats->num_STOREs);
    printf(INDENT4 "STOREs without paddr: %lu\n", stats->num_STOREs_no_paddr);
    printf(INDENT4 "STOREs with invalid paddr: %lu\n", stats->num_STOREs_invalid_paddr);
    printf("\n");
    printf(INDENT4 "CLOADs: %lu\n", stats->num_CLOADs);
    printf(INDENT4 "CLOADs without paddr: %lu\n", stats->num_CLOADs_no_paddr);
    printf(INDENT4 "CLOADs with invalid paddr: %lu\n", stats->num_CLOADs_invalid_paddr);
    printf("\n");
    printf(INDENT4 "CSTOREs: %lu\n", stats->num_CSTOREs);
    printf(INDENT4 "CSTOREs without paddr: %lu\n", stats->num_CSTOREs_no_paddr);
    printf(INDENT4 "CSTOREs with invalid paddr: %lu\n", stats->num_CSTOREs_invalid_paddr);
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

    trace_reader_t input_trace =
        trace_reader_open(arena, input_filename, TRACE_READER_TYPE_UNCOMPRESSED_OR_GZIP);

    trace_stats_t global_stats = {0};

    while (true)
    {
        custom_trace_entry_t current_entry;
        if (!trace_reader_get(&input_trace, &current_entry, sizeof(current_entry))) break;

        update_trace_stats(&global_stats, current_entry);
    }

    print_trace_stats(&global_stats);

    trace_reader_close(&input_trace);
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

    if (file_exists_not_fifo(output_filename))
    {
        printf("ERROR: Attempted to overwrite existing file \"%s\".\n", output_filename);
        quit();
    }

    trace_reader_t input_trace =
        trace_reader_open(arena, input_filename, guess_reader_type(input_filename));

    trace_writer_t output_trace =
        trace_writer_open(arena, output_filename, guess_writer_type(output_filename));

    map_u64 page_table = map_u64_create();
    set_u64 dbg_pages_changed_mapping = set_u64_create();
    set_u64 dbg_pages_without_mapping = set_u64_create();

    trace_stats_t global_stats_before = {0};
    trace_stats_t global_stats_after = {0};

    u64 dbg_num_addr_mapping_changes = 0;

    // TODO could output compressed traces that omit vaddrs? maybe we should have a header with some metadata in it

    while (true)
    {
        custom_trace_entry_t current_entry;
        if (!trace_reader_get(&input_trace, &current_entry, sizeof(current_entry))) break;

        update_trace_stats(&global_stats_before, current_entry);

        u64 vaddr = current_entry.vaddr;
        u64 paddr = current_entry.paddr;
        assert(vaddr);

        if (check_paddr_valid(paddr))
        {
            u64 paddr_page;
            if (map_u64_get(page_table, get_page_start(vaddr), &paddr_page))
            {
                // TODO why is virtual-physical mapping changing during execution (userspace traces)?
                if (paddr_page != get_page_start(paddr))
                {
                    dbg_num_addr_mapping_changes++;
                    set_u64_insert(dbg_pages_changed_mapping, get_page_start(vaddr));
                }
            }

            map_u64_set(page_table, get_page_start(vaddr), get_page_start(paddr));
        }
        else
        {
            u64 paddr_page;
            if (map_u64_get(page_table, get_page_start(vaddr), &paddr_page))
            {
                assert(paddr_page);

                current_entry.paddr = paddr_page + (vaddr - get_page_start(vaddr));
            }
            else
            {
                // NOTE to see how many pages the entries without valid mappings correspond to
                set_u64_insert(dbg_pages_without_mapping, get_page_start(vaddr));
            }
        }

        trace_writer_emit(&output_trace, &current_entry, sizeof(current_entry));

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
    printf("Pages with mapping changes: %lu\n", set_u64_size(dbg_pages_changed_mapping));
    printf("Pages without paddr mappings: %lu\n", set_u64_size(dbg_pages_without_mapping));

    trace_reader_close(&input_trace);
    trace_writer_close(&output_trace);

    map_u64_cleanup(&page_table);
    set_u64_cleanup(&dbg_pages_changed_mapping);
    set_u64_cleanup(&dbg_pages_without_mapping);
}

void trace_convert(COMMAND_HANDLER_ARGS)
{
    if (num_args != 2)
    {
        printf("Usage: %s %s <input file> <output file>\n", exe_name, cmd_name);
        quit();
    }

    char * input_filename = args[0];
    char * output_filename = args[1];

    if (file_exists_not_fifo(output_filename))
    {
        if (!confirm_overwrite_file(output_filename)) quit();
    }

    trace_reader_t input_trace =
        trace_reader_open(arena, input_filename, guess_reader_type(input_filename));
    trace_writer_t output_trace =
        trace_writer_open(arena, output_filename, guess_writer_type(output_filename));

    trace_stats_t global_stats = {0};

    while (true)
    {
        custom_trace_entry_t current_entry;
        if (!trace_reader_get(&input_trace, &current_entry, sizeof(current_entry))) break;

        update_trace_stats(&global_stats, current_entry);

        trace_writer_emit(&output_trace, &current_entry, sizeof(current_entry));
    }

    print_trace_stats(&global_stats);

    trace_reader_close(&input_trace);
    trace_writer_close(&output_trace);
}

void trace_convert_generic(COMMAND_HANDLER_ARGS)
{
    if (num_args != 2)
    {
        printf("Usage: %s %s <input file> <output file>\n", exe_name, cmd_name);
        quit();
    }

    char * input_filename = args[0];
    char * output_filename = args[1];

    if (file_exists_not_fifo(output_filename))
    {
        if (!confirm_overwrite_file(output_filename)) quit();
    }

    trace_reader_t input_trace =
        trace_reader_open(arena, input_filename, guess_reader_type(input_filename));
    trace_writer_t output_trace =
        trace_writer_open(arena, output_filename, guess_writer_type(output_filename));

    while (true)
    {
        u8 current_byte;
        if (!trace_reader_get(&input_trace, &current_byte, 1)) break;

        trace_writer_emit(&output_trace, &current_byte, 1);
    }

    trace_reader_close(&input_trace);
    trace_writer_close(&output_trace);
}

// TODO move main loops here into drcachesim source file?
void trace_convert_drcachesim_vaddr(COMMAND_HANDLER_ARGS)
{
    if (num_args != 2)
    {
        printf("Usage: %s %s <input custom trace file> <output drcachesim trace file>\n", exe_name, cmd_name);
        quit();
    }

    char * input_trace_filename = args[0];
    char * output_trace_filename = args[1];

    if (file_exists_not_fifo(output_trace_filename))
    {
        if (!confirm_overwrite_file(output_trace_filename)) quit();
    }

    trace_reader_t input_trace =
        trace_reader_open(arena, input_trace_filename, guess_reader_type(input_trace_filename));

    trace_writer_t output_trace =
        trace_writer_open(arena, output_trace_filename, guess_writer_type(output_trace_filename));

    u64 dbg_paddrs_invalid = 0;
    map_u64 page_table = map_u64_create();

    write_drcachesim_header(&output_trace);

    while (true)
    {
        custom_trace_entry_t current_entry;
        if (!trace_reader_get(&input_trace, &current_entry, sizeof(current_entry))) break;

        // skip invalid entries
        if (!check_paddr_valid(current_entry.paddr))
        {
            dbg_paddrs_invalid++;
            continue;
        }

        write_drcachesim_trace_entry_vaddr(&output_trace, page_table, current_entry);
    }

    write_drcachesim_footer(&output_trace);

    printf("Entries with invalid paddrs (skipped): %ld\n", dbg_paddrs_invalid);

    map_u64_cleanup(&page_table);

    trace_reader_close(&input_trace);
    trace_writer_close(&output_trace);
}

void trace_convert_drcachesim_paddr(COMMAND_HANDLER_ARGS)
{
    if (num_args != 2)
    {
        printf("Usage: %s %s <input custom trace file> <output drcachesim trace file>\n", exe_name, cmd_name);
        quit();
    }

    char * input_trace_filename = args[0];
    char * output_trace_filename = args[1];

    if (file_exists_not_fifo(output_trace_filename))
    {
        if (!confirm_overwrite_file(output_trace_filename)) quit();
    }

    trace_reader_t input_trace =
        trace_reader_open(arena, input_trace_filename, guess_reader_type(input_trace_filename));

    trace_writer_t output_trace =
        trace_writer_open(arena, output_trace_filename, guess_writer_type(output_trace_filename));

    u64 dbg_paddrs_invalid = 0;

    write_drcachesim_header(&output_trace);

    while (true)
    {
        custom_trace_entry_t current_entry;
        if (!trace_reader_get(&input_trace, &current_entry, sizeof(current_entry))) break;

        // skip invalid entries
        if (!check_paddr_valid(current_entry.paddr))
        {
            dbg_paddrs_invalid++;
            continue;
        }

        write_drcachesim_trace_entry_paddr(&output_trace, current_entry);
    }

    write_drcachesim_footer(&output_trace);

    printf("Entries with invalid paddrs (skipped): %ld\n", dbg_paddrs_invalid);

    trace_reader_close(&input_trace);
    trace_writer_close(&output_trace);
}


void trace_get_initial_accesses(COMMAND_HANDLER_ARGS)
{
    if (num_args != 2)
    {
        printf("Usage: %s %s <input trace file> <output bin file>\n", exe_name, cmd_name);
        quit();
    }

    char * input_trace_filename = args[0];
    char * initial_state_filename = args[1];

    if (file_exists_not_fifo(initial_state_filename))
    {
        if (!confirm_overwrite_file(initial_state_filename)) quit();
    }

    trace_reader_t input_trace =
        trace_reader_open(arena, input_trace_filename, guess_reader_type(input_trace_filename));

    FILE * initial_state_file = fopen(initial_state_filename, "wb");
    assert(initial_state_file);

    static_assert(sizeof(initial_access_t) == 1, "Expect initial access record type to be a single byte.");

    i64 initial_state_table_size = MEMORY_SIZE / CAP_SIZE_BYTES;
    initial_access_t * initial_state_table = arena_push_array(arena, initial_access_t, initial_state_table_size);
    for (i64 i = 0; i < initial_state_table_size; i++)
    {
        // TODO get some stats

        initial_state_table[i].type = -1;
        initial_state_table[i].tag = 0;
    }

    i64 dbg_paddrs_missing = 0;
    i64 dbg_paddrs_invalid = 0;

    // TODO check for supposedly impossible cases (CLOAD -> no modification -> CLOAD with different tag)?

    while (true)
    {
        custom_trace_entry_t current_entry;
        if (!trace_reader_get(&input_trace, &current_entry, sizeof(current_entry))) break;

        if (!check_paddr_valid(current_entry.paddr))
        {
            if (current_entry.paddr == -1) dbg_paddrs_missing++;
            else dbg_paddrs_invalid++;

            continue;
        }

        assert(current_entry.tag == 0 || current_entry.tag == 1);

        u64 start_addr = align_floor_pow_2(current_entry.paddr, CAP_SIZE_BYTES);
        u64 end_addr = align_ceil_pow_2(current_entry.paddr + current_entry.size, CAP_SIZE_BYTES);
        assert(start_addr < end_addr);

        for (u64 paddr = start_addr; paddr < end_addr; paddr += CAP_SIZE_BYTES)
        {
            assert(check_paddr_valid(paddr));
            u64 mem_offset = paddr - BASE_PADDR;

            assert(mem_offset % CAP_SIZE_BYTES == 0);
            i64 table_idx = mem_offset / CAP_SIZE_BYTES;
            assert(table_idx >= 0 && table_idx < initial_state_table_size);

            if (initial_state_table[table_idx].type == -1)
            {
                switch (current_entry.type)
                {
                    case CUSTOM_TRACE_TYPE_INSTR:
                    case CUSTOM_TRACE_TYPE_STORE:
                    case CUSTOM_TRACE_TYPE_LOAD:
                        assert(current_entry.tag == 0);
                        break;
                }
                assert(current_entry.tag == 0 || current_entry.tag == 1);

                initial_state_table[table_idx].type = current_entry.type;
                initial_state_table[table_idx].tag = current_entry.tag;
            }
        }
    }

    printf("Entries missing paddrs (skipped): %ld\n", dbg_paddrs_missing);
    printf("Entries with invalid paddrs (skipped): %ld\n", dbg_paddrs_invalid);

    fwrite(initial_state_table, 1, initial_state_table_size, initial_state_file);
    fclose(initial_state_file);

    trace_reader_close(&input_trace);
}


static inline u8 get_tag_idx(u64 cache_line_addr, u64 cap_addr)
{
    assert(cap_addr >= cache_line_addr);
    u64 offset = cap_addr - cache_line_addr;
    assert(offset < CACHE_LINE_SIZE);
    assert(offset / CAP_SIZE_BYTES <= 8);

    u8 tag_idx = offset / CAP_SIZE_BYTES;

    return tag_idx;
}

static inline void tag_set(tags_t * tags_cheri, u8 tag_idx, u8 value)
{
    if (value)
    {
        tags_cheri->data |= (1 << tag_idx);
    }
    else
    {
        tags_cheri->data &= ~(1 << tag_idx);
    }

    tags_cheri->known |= (1 << tag_idx);
}

void trace_simulate(COMMAND_HANDLER_ARGS)
{
    if (num_args != 2)
    {
        printf("Usage: %s %s <trace file> <output file>\n", exe_name, cmd_name);
        quit();
    }

    char * input_trace_filename = args[0];
    char * output_requests_filename = args[1];

    device_t * tag_controller = controller_interface_init(arena, output_requests_filename);
    device_t * l2_cache = cache_init(arena, "L2", KILOBYTES(1024), 8, tag_controller);

    device_t * l1_instr_cache = cache_init(arena, "L1I", KILOBYTES(64), 4, l2_cache);
    device_t * l1_data_cache = cache_init(arena, "L1D", KILOBYTES(64), 4, l2_cache);
    assert(l2_cache->num_children == 2);

    device_t * all_devices[] =
    {
        l1_instr_cache,
        l1_data_cache,
        l2_cache,
        tag_controller
    };
    assert(array_count(all_devices) <= UINT32_MAX);
    u32 num_devices = array_count(all_devices);

    printf("Simulating with following configuration:\n");
    for (i64 i = 0; i < num_devices; i++)
    {
        device_print_configuration(all_devices[i]);
    }
    printf("\n");

    trace_reader_t input_trace =
        trace_reader_open(arena, input_trace_filename, guess_reader_type(input_trace_filename));

    i64 dbg_paddrs_missing = 0;
    i64 dbg_paddrs_invalid = 0;

    while (true)
    {
        custom_trace_entry_t current_entry;
        if (!trace_reader_get(&input_trace, &current_entry, sizeof(current_entry))) break;

        /* NOTE can just assume all caches are PIPT.
         * VIPT relies on the fact that the lowest bits of the physical and virtual addresses are the same,
         * so this will have no affect on indexing.
         */

        if (!check_paddr_valid(current_entry.paddr))
        {
            if (current_entry.paddr == 0) dbg_paddrs_missing++;
            else dbg_paddrs_invalid++;

            continue;
        }

        u64 start_addr = align_floor_pow_2(current_entry.paddr, CACHE_LINE_SIZE);
        u64 end_addr = align_ceil_pow_2(current_entry.paddr + current_entry.size, CACHE_LINE_SIZE);
        for (u64 paddr = start_addr; paddr < end_addr; paddr += CACHE_LINE_SIZE)
        {
            u64 start_addr_cap = align_floor_pow_2(current_entry.paddr, CAP_SIZE_BYTES);
            u64 end_addr_cap = align_ceil_pow_2(current_entry.paddr + current_entry.size, CAP_SIZE_BYTES);

            if (start_addr_cap < paddr) start_addr_cap = paddr;
            if (end_addr_cap > paddr + CACHE_LINE_SIZE) end_addr_cap = paddr + CACHE_LINE_SIZE;

            assert(start_addr_cap % CAP_SIZE_BYTES == 0);
            assert(end_addr_cap % CAP_SIZE_BYTES == 0);

            if (current_entry.type == CUSTOM_TRACE_TYPE_INSTR)
            {
                cache_line_t * cache_line = cache_request(l1_instr_cache, paddr);

                assert(current_entry.tag == 0);

                assert(cache_line->dirty == false);

                for (u64 paddr_cap = start_addr_cap; paddr_cap < end_addr_cap; paddr_cap += CAP_SIZE_BYTES)
                {
                    u8 tag_idx = get_tag_idx(paddr, paddr_cap);

                    tag_set(&cache_line->tags_cheri, tag_idx, 0);

                    assert((cache_line->tags_cheri.data & (1 << tag_idx)) == 0);
                    assert((cache_line->tags_cheri.known & (1 << tag_idx)) != 0);
                }
            }
            else
            {
                cache_line_t * cache_line = cache_request(l1_data_cache, paddr);

                switch (current_entry.type)
                {
                    case CUSTOM_TRACE_TYPE_LOAD: break;
                    case CUSTOM_TRACE_TYPE_CLOAD:
                    {
                        for (u64 paddr_cap = start_addr_cap; paddr_cap < end_addr_cap; paddr_cap += CAP_SIZE_BYTES)
                        {
                            u8 tag_idx = get_tag_idx(paddr, paddr_cap);

                            // if the tag is already "known", then this should match what was there before
                            assert((cache_line->tags_cheri.known & (1 << tag_idx)) == 0 ||
                                ((cache_line->tags_cheri.data & (1 << tag_idx)) != 0) == current_entry.tag);

                            tag_set(&cache_line->tags_cheri, tag_idx, current_entry.tag);

                            assert(((cache_line->tags_cheri.data & (1 << tag_idx)) != 0) == current_entry.tag);
                            assert((cache_line->tags_cheri.known & (1 << tag_idx)) != 0);
                        }
                    } break;
                    case CUSTOM_TRACE_TYPE_STORE:
                    case CUSTOM_TRACE_TYPE_CSTORE:
                    {
                        // NOTE for the CPU caches, we don't check if the data actually changed
                        // TODO for the tag cache, we should check if the data actually changed before setting to dirty
                        cache_line->dirty = true;

                        assert(current_entry.type != CUSTOM_TRACE_TYPE_STORE || current_entry.tag == 0);
                        for (u64 paddr_cap = start_addr_cap; paddr_cap < end_addr_cap; paddr_cap += CAP_SIZE_BYTES)
                        {
                            u8 tag_idx = get_tag_idx(paddr, paddr_cap);

                            tag_set(&cache_line->tags_cheri, tag_idx, current_entry.tag);

                            assert(((cache_line->tags_cheri.data & (1 << tag_idx)) != 0) == current_entry.tag);
                            assert((cache_line->tags_cheri.known & (1 << tag_idx)) != 0);
                        }
                    } break;
                    default: assert(!"Impossible.");
                }
            }
        }
    }

    printf("Entries missing paddrs (skipped): %ld\n", dbg_paddrs_missing);
    printf("Entries with invalid paddrs (skipped): %ld\n", dbg_paddrs_invalid);
    printf("\n");

    printf("Statistics:\n");
    for (i64 i = 0; i < num_devices; i++)
    {
        device_print_statistics(all_devices[i]);
    }
    printf("\n");

    for (i64 i = 0; i < num_devices; i++)
    {
        device_cleanup(all_devices[i]);
    }

    trace_reader_close(&input_trace);

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

// TODO fix
void trace_simulate_uncompressed(COMMAND_HANDLER_ARGS)
{
    if (num_args != 1)
    {
        printf("Usage: %s %s <controller trace file>\n", exe_name, cmd_name);
        quit();
    }

    char * input_trace_filename = args[0];
    // char * initial_tags_filename = args[1];

    // device_t * tag_controller = tag_cache_init(arena, initial_tags_filename, KILOBYTES(32), 4);

    trace_reader_t input_trace =
        trace_reader_open(arena, input_trace_filename, TRACE_READER_TYPE_UNCOMPRESSED_OR_GZIP);

    // printf("Simulating with following configuration:\n");
    // device_print_configuration(tag_controller);
    // printf("\n");

    while (true)
    {
        tag_cache_request_t current_entry;
        if (!trace_reader_get(&input_trace, &current_entry, sizeof(current_entry))) break;

        assert(current_entry.size == CACHE_LINE_SIZE);

        u64 paddr = current_entry.addr;
        assert(check_paddr_valid(paddr));
        assert(paddr % CACHE_LINE_SIZE == 0);
        assert(paddr % CAP_SIZE_BYTES == 0);

        // TODO support larger cache line sizes?
        assert(current_entry.tags == (u8) current_entry.tags);
        // b8 tags_cheri = current_entry.tags;
        // b8 tags_known = current_entry.tags_known; // TODO again, makes no sense

        printf("%-6s [ addr: " FMT_ADDR ", tags data: %02X, tags known mask: %02X ]\n",
            current_entry.type == TAG_CACHE_REQUEST_TYPE_READ ? "READ" :
            current_entry.type == TAG_CACHE_REQUEST_TYPE_WRITE ? "WRITE" : "UNKNOWN",
            current_entry.addr, current_entry.tags, current_entry.tags_known);

        switch (current_entry.type)
        {
            case TAG_CACHE_REQUEST_TYPE_READ:
            {
                // TODO this makes no sense, fix
                // b8 tags_read, tags_known_read;
                // device_read(tag_controller, paddr, &tags_read, &tags_known_read);
                // assert(tags_read == tags_cheri);
            } break;
            case TAG_CACHE_REQUEST_TYPE_WRITE:
            {
                // device_write(tag_controller, paddr, tags_cheri, tags_known);
            } break;
            default: assert(!"Impossible.");
        }
    }

    // printf("Statistics:\n");
    // device_print_statistics(tag_controller);
    // device_cleanup(tag_controller);
    // printf("\n");

    trace_reader_close(&input_trace);
}
