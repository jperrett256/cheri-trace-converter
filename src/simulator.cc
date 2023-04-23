#include "jdp.h"
#include "common.h"
#include "utils.h"
#include "trace.h"
#include "simulator.h"

#include <stdio.h>

// TODO do actual tag cache simulation
// TODO alternatively, could output requests to tag cache, and run the tag cache simulation in a separate pass (might massively save time)
// TODO should definitely try lz4

// TODO move function(s) up?
static void tag_cache_interface_write_entry(device_t * device, u8 type, u64 paddr, b8 tags);

static void device_write(device_t * device, u64 paddr, b8 tags_cheri)
{
    switch (device->type)
    {
        case DEVICE_TYPE_CACHE:
        {
            cache_line_t * cache_line = cache_lookup(device, paddr);
            cache_line->tags_cheri = tags_cheri;
            // TODO dirty bit?
        } break;
        case DEVICE_TYPE_TAG_CACHE_INTERFACE:
        {
            device->tag_cache_interface.stats.writes++;
            tag_cache_interface_write_entry(device, TAG_CACHE_REQUEST_TYPE_WRITE, paddr, tags_cheri);

            assert(paddr % CACHE_LINE_SIZE == 0);
            assert(paddr % CAP_SIZE_BYTES == 0);

            assert(CACHE_LINE_SIZE / CAP_SIZE_BYTES == 8); // TODO handle other cache line sizes

            assert(check_paddr_valid(paddr));
            u64 mem_offset = paddr - BASE_PADDR;
            i64 tag_table_idx = mem_offset / CAP_SIZE_BYTES / 8;
            assert(tag_table_idx >= 0 && tag_table_idx < device->tag_cache_interface.tags_size);

            device->tag_cache_interface.tags[tag_table_idx] = tags_cheri;
            // // TODO can definitely optimise, no need to copy bit by bit (especially if each cache line has 8 tag bits)
            // for (u64 paddr_cap = paddr; paddr_cap < paddr + CACHE_LINE_SIZE; paddr_cap += CAP_SIZE_BYTES)
            // {
            //     // TODO utility function for getting tag idx and tag bit?
            //     assert(check_paddr_valid(paddr));
            //     u64 mem_offset = paddr - BASE_PADDR;

            //     i64 tag_table_idx = mem_offset / CAP_SIZE_BYTES / 8;
            //     i8 tag_entry_bit = mem_offset / CAP_SIZE_BYTES % 8;
            //     assert(tag_table_idx >= 0 && tag_table_idx < device->tag_cache_interface.tags_size);
            //     assert(tag_entry_bit >= 0 && tag_entry_bit < 8);

            //     // initial_tag_state[tag_table_idx] |= 1 << tag_entry_bit;
            // }
        } break;
        default: assert(!"Impossible.");
    }
}

static b8 device_read(device_t * device, u64 paddr)
{
    switch (device->type)
    {
        case DEVICE_TYPE_CACHE:
        {
            cache_line_t * cache_line = cache_lookup(device, paddr);
            return cache_line->tags_cheri;
            // TODO dirty bit?
        } break;
        case DEVICE_TYPE_TAG_CACHE_INTERFACE:
        {
            device->tag_cache_interface.stats.reads++;

            assert(paddr % CACHE_LINE_SIZE == 0);
            assert(paddr % CAP_SIZE_BYTES == 0);

            assert(CACHE_LINE_SIZE / CAP_SIZE_BYTES == 8); // TODO handle other cache line sizes

            assert(check_paddr_valid(paddr));
            u64 mem_offset = paddr - BASE_PADDR;
            i64 tag_table_idx = mem_offset / CAP_SIZE_BYTES / 8;
            assert(tag_table_idx >= 0 && tag_table_idx < device->tag_cache_interface.tags_size);

            b8 tags_cheri = device->tag_cache_interface.tags[tag_table_idx];

            tag_cache_interface_write_entry(device, TAG_CACHE_REQUEST_TYPE_WRITE, paddr, tags_cheri);

            return tags_cheri;
        } break;
        default: assert(!"Impossible.");
    }

    assert(!"Impossible.");
    return 0;
}

static void fill_initial_tags(char * initial_tags_filename, u32 tags_buffer_size, u8 * tags_buffer)
{
    FILE * initial_tags_file = fopen(initial_tags_filename, "rb");
    assert(initial_tags_file); // TODO error instead?

    size_t bytes_read = fread(tags_buffer, sizeof(u8), tags_buffer_size, initial_tags_file);
    assert(bytes_read == tags_buffer_size);
    fclose(initial_tags_file);
}

/*
device_t tag_cache_init(arena_t * arena, char * initial_tags_filename)
{
    device_t device = {0};
    device.type = DEVICE_TYPE_TAG_CACHE;

    device.tag_cache.tags_size = MEMORY_SIZE / CAP_SIZE_BYTES / 8;
    device.tag_cache.tags = arena_push_array(arena, u8, device.tag_cache.tags_size);
    fill_initial_tags(initial_tags_filename, device.tag_cache.tags_size, device.tag_cache.tags);

    // TODO

    return device;
}
*/

device_t tag_cache_interface_init(arena_t * arena, char * initial_tags_filename, char * output_filename)
{
    device_t device = {0};
    device.type = DEVICE_TYPE_TAG_CACHE_INTERFACE;

    // TODO move these two lines into the fill_initial_tags function as well?
    device.tag_cache_interface.tags_size = MEMORY_SIZE / CAP_SIZE_BYTES / 8;
    device.tag_cache_interface.tags = arena_push_array(arena, u8, device.tag_cache_interface.tags_size);
    fill_initial_tags(initial_tags_filename, device.tag_cache_interface.tags_size, device.tag_cache_interface.tags);

    assert(output_filename);
    if (file_exists(output_filename))
    {
        // TODO ask for confirmation instead?
        printf("ERROR: Attempted to overwrite existing file \"%s\".\n", output_filename);
        quit();
    }

    device.tag_cache_interface.output = gzopen(output_filename, "wb");
    assert(device.tag_cache_interface.output);

    return device;
}

static void tag_cache_interface_write_entry(device_t * device, u8 type, u64 paddr, b8 tags)
{
    assert(device->type == DEVICE_TYPE_TAG_CACHE_INTERFACE);

    tag_cache_request_t request = {0};
    request.type = type;
    request.paddr = paddr;
    request.tags = tags;
    static_assert(CACHE_LINE_SIZE <= UINT16_MAX, "Invalid cache line size.");
    request.size = CACHE_LINE_SIZE;

    assert(device->tag_cache_interface.output);
    int bytes_written = gzwrite(device->tag_cache_interface.output, &request, sizeof(request));
    assert(bytes_written == sizeof(request));
}

static void tag_cache_interface_cleanup(device_t * device)
{
    assert(device->type == DEVICE_TYPE_TAG_CACHE_INTERFACE);

    assert(device->tag_cache_interface.output);
    gzclose(device->tag_cache_interface.output);
}

/* TODO if you get rid of C++, review uses of const */
device_t cache_init(arena_t * arena, const char * name, u32 size, u32 num_ways, device_t * parent)
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

    assert(name);
    device.cache.name = (char *) name; // TODO get rid of C++?

    return device;
}

cache_line_t * cache_lookup(device_t * device, u64 paddr)
{
    assert(device->type == DEVICE_TYPE_CACHE);

    assert(paddr % CACHE_LINE_SIZE == 0);

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
    if (way >= 0)
    {
        device->cache.stats.hits++;

        result = &device->cache.entries[set_start_idx + way];
    }
    else
    {
        device->cache.stats.misses++;

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

static char * device_get_name(device_t * device)
{
    switch (device->type)
    {
        case DEVICE_TYPE_CACHE:
        {
            assert(device->cache.name);
            return device->cache.name;
        } break;
        // case DEVICE_TYPE_TAG_CACHE:
        // {
        //     return (char *) "TAG CONTROLLER"; // TODO get rid of C++?
        // } break;
        case DEVICE_TYPE_TAG_CACHE_INTERFACE:
        {
            return (char *) "TAG CONTROLLER (INTERFACE)"; // TODO get rid of C++?
        } break;
        default: assert(!"Impossible.");
    }

    assert(!"Impossible.");
    return NULL;
}

void device_print_configuration(device_t * device)
{
    switch (device->type)
    {
        case DEVICE_TYPE_CACHE:
        {
            printf(INDENT4 "%s (%u-way, %u bytes) -> %s\n",
                device_get_name(device), device->cache.num_ways, device->cache.size,
                device->parent ? device_get_name(device->parent) : "NULL");
        } break;
        // case DEVICE_TYPE_TAG_CACHE: // TODO associativity, size?
        case DEVICE_TYPE_TAG_CACHE_INTERFACE:
        {
            printf(INDENT4 "%s\n", device_get_name(device));
        } break;
        default: assert(!"Impossible.");
    }
}

void device_print_statistics(device_t * device)
{
    printf(INDENT4 "%s:\n", device_get_name(device));

    switch (device->type)
    {
        case DEVICE_TYPE_CACHE:
        {
            printf(INDENT8 "Hits: %lu\n", device->cache.stats.hits);
            printf(INDENT8 "Misses: %lu\n", device->cache.stats.misses);

            printf(INDENT8 "Miss rate: %f\n",
                (double) device->cache.stats.misses / (device->cache.stats.hits + device->cache.stats.misses));
        } break;
        case DEVICE_TYPE_TAG_CACHE_INTERFACE:
        {
            printf(INDENT8 "Reads: %lu\n", device->tag_cache_interface.stats.reads);
            printf(INDENT8 "Writes: %lu\n", device->tag_cache_interface.stats.writes);
        } break;
        default: assert(!"Impossible.");
    }
}

void device_cleanup(device_t * device)
{
    switch (device->type)
    {
        case DEVICE_TYPE_TAG_CACHE_INTERFACE:
        {
            tag_cache_interface_cleanup(device);
        } break;
        case DEVICE_TYPE_CACHE:
            break;
        default: assert(!"Impossible.");
    }
}
