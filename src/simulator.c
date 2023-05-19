#include "jdp.h"
#include "common.h"
#include "utils.h"
#include "trace.h"
#include "simulator.h"

#include <stdio.h>

// TODO do actual tag cache simulation
// TODO alternatively, could output requests to tag cache, and run the tag cache simulation in a separate pass (might massively save time)

static void cache_write_back(device_t * device, u64 paddr, b8 tags_cheri, b8 tags_known);
static void cache_write_back_invisible(device_t * device, u64 paddr, b8 tags_cheri, b8 tags_known);

static void get_initial_tags(char * initial_tags_filename, u32 tags_buffer_size, u8 * tags_buffer)
{
    FILE * initial_tags_file = fopen(initial_tags_filename, "rb");
    assert(initial_tags_file); // TODO error instead?

    size_t bytes_read = fread(tags_buffer, sizeof(u8), tags_buffer_size, initial_tags_file);
    assert(bytes_read == tags_buffer_size);
    fclose(initial_tags_file);
}

device_t * tag_cache_init(arena_t * arena, char * initial_tags_filename, u32 size_bytes, u32 num_ways)
{
    device_t * device = arena_push(arena, sizeof(device_t));
    *device = (device_t) {0};
    device->type = DEVICE_TYPE_TAG_CACHE;

    device->tag_cache.tags_size = MEMORY_SIZE / CAP_SIZE_BYTES / 8;
    device->tag_cache.tags = arena_push_array(arena, u8, device->tag_cache.tags_size);
    get_initial_tags(initial_tags_filename, device->tag_cache.tags_size, device->tag_cache.tags);

    assert(size_bytes % CACHE_LINE_SIZE == 0);
    u32 size = size_bytes / CACHE_LINE_SIZE;

    device->tag_cache.size = size;
    device->tag_cache.num_ways = num_ways;
    assert(size % num_ways == 0);

    device->tag_cache.entries = arena_push_array(arena, cache_line_t, size);

    for (i64 set_start_idx = 0; set_start_idx < size; set_start_idx += num_ways)
    for (i64 way = 0; way < num_ways; way++)
    {
        assert(set_start_idx + way < size);
        device->tag_cache.entries[set_start_idx + way].tag_addr = INVALID_TAG;
        device->tag_cache.entries[set_start_idx + way].counter = way; // LRU replacement policy

        device->tag_cache.entries[set_start_idx + way].tags_cheri = 0; // not used
    }


    return device;
}

device_t * controller_interface_init(arena_t * arena, char * output_filename)
{
    device_t * device = arena_push(arena, sizeof(device_t));
    *device = (device_t) {0};
    device->type = DEVICE_TYPE_CONTROLLER_INTERFACE;

    u32 tag_table_size = MEMORY_SIZE / CAP_SIZE_BYTES / 8;
    device->controller_interface.tags_size = tag_table_size;
    device->controller_interface.tags = arena_push_array(arena, u8, tag_table_size);
    device->controller_interface.tags_known = arena_push_array(arena, u8, tag_table_size);
    for (i64 i = 0; i < tag_table_size; i++)
    {
        device->controller_interface.tags[i] = 0;
        device->controller_interface.tags_known[i] = 0;
    }

    assert(output_filename);
    if (file_exists(output_filename))
    {
        if (!confirm_overwrite_file(output_filename)) quit();
    }

    device->controller_interface.output = trace_writer_open(arena, output_filename, guess_writer_type(output_filename));

    return device;
}

static void controller_interface_write_entry(device_t * device, u8 type, u64 paddr, b8 tags, b8 tags_known)
{
    assert(device->type == DEVICE_TYPE_CONTROLLER_INTERFACE);

    // TODO make tags and tag_known u16 to support larger cache line sizes?

    tag_cache_request_t request = {0};
    request.type = type;
    request.addr = paddr;
    assert((tags & ~tags_known) == 0);
    request.tags = tags;
    request.tags_known = tags_known;
    static_assert(CACHE_LINE_SIZE <= UINT16_MAX, "Invalid cache line size.");
    request.size = CACHE_LINE_SIZE;

    trace_writer_emit(&device->controller_interface.output, &request, sizeof(request));
}

static void controller_interface_cleanup(device_t * device)
{
    assert(device->type == DEVICE_TYPE_CONTROLLER_INTERFACE);

    trace_writer_close(&device->controller_interface.output);
}

static inline void tag_table_get_index(u32 tag_table_size, u64 paddr,
    u64 * tag_table_idx_ptr, u32 * first_bit_ptr, u32 * clear_mask_ptr)
{
    assert(paddr % CACHE_LINE_SIZE == 0);
    assert(paddr % CAP_SIZE_BYTES == 0);

    assert(check_paddr_valid(paddr));
    u64 mem_offset = paddr - BASE_PADDR;
    i64 tag_table_idx = mem_offset / CAP_SIZE_BYTES / 8;
    u32 first_bit = mem_offset / CAP_SIZE_BYTES % 8;
    assert(tag_table_idx >= 0 && tag_table_idx < tag_table_size);
    assert(first_bit >= 0 && first_bit < 8);

    u32 num_bits = CACHE_LINE_SIZE / CAP_SIZE_BYTES;
    assert(num_bits <= 8 && num_bits >= 1);
    u32 clear_mask = ((u32) -1) << num_bits;
    clear_mask <<= first_bit;
    clear_mask |= (1 << first_bit) - 1;

    *tag_table_idx_ptr = tag_table_idx;
    *first_bit_ptr = first_bit;
    *clear_mask_ptr = clear_mask;
}

static b8 tag_table_read(u8 * tag_table, u32 tag_table_size, u64 paddr)
{
    u64 tag_table_idx;
    u32 first_bit, clear_mask;
    tag_table_get_index(tag_table_size, paddr, &tag_table_idx, &first_bit, &clear_mask);

    b8 tags_cheri = (tag_table[tag_table_idx] & ~clear_mask) >> first_bit;
    assert((tags_cheri << first_bit) == ((tags_cheri << first_bit) & ~clear_mask));

    return tags_cheri;
}

static void tag_table_write(u8 * tag_table, u32 tag_table_size, u64 paddr, b8 tags_cheri)
{
    u64 tag_table_idx;
    u32 first_bit, clear_mask;
    tag_table_get_index(tag_table_size, paddr, &tag_table_idx, &first_bit, &clear_mask);

    tag_table[tag_table_idx] &= clear_mask;
    assert((tags_cheri << first_bit) == ((tags_cheri << first_bit) & ~clear_mask));
    tag_table[tag_table_idx] |= tags_cheri << first_bit;
    assert(((tag_table[tag_table_idx] & ~clear_mask) >> first_bit) == tags_cheri);
}

static void tag_cache_record_access(device_t * device, u64 paddr)
{
    assert(paddr % CACHE_LINE_SIZE == 0);

    // TODO make normal cache utility function more generic?
    u64 tag_addr = paddr >> CACHE_LINE_SIZE_BITS;
    u32 num_sets = device->tag_cache.size / device->tag_cache.num_ways;
    u32 set_start_idx = (tag_addr % num_sets) * device->tag_cache.num_ways;

    assert(tag_addr != INVALID_TAG);

    i64 way = -1;
    for (i64 i = 0; i < device->tag_cache.num_ways; i++)
    {
        if (device->tag_cache.entries[set_start_idx + i].tag_addr == tag_addr)
        {
            assert(way == -1);
            way = i;
        }
    }

    cache_line_t * result = NULL;
    if (way >= 0)
    {
        device->tag_cache.stats.hits++;

        result = &device->cache.entries[set_start_idx + way];
    }
    else
    {
        device->tag_cache.stats.misses++;

        // TODO utility function?
        // choose a way to fill next
        way = -1;
        u16 largest_counter = 0;
        for (i64 i = 0; i < device->tag_cache.num_ways; i++)
        {
            if (device->tag_cache.entries[set_start_idx + i].tag_addr == INVALID_TAG)
            {
                way = i;
                break;
            }

            u16 current_counter = device->tag_cache.entries[set_start_idx + i].counter;
            if (current_counter >= largest_counter)
            {
                assert(current_counter != largest_counter || largest_counter == 0);
                largest_counter = current_counter;
                way = i;
            }
        }
        assert(way >= 0);

        cache_line_t * line_to_replace = &device->tag_cache.entries[set_start_idx + way];
        if (line_to_replace->tag_addr != INVALID_TAG)
        {
            if (line_to_replace->dirty)
            {
                device->tag_cache.stats.write_backs++;
            }
        }

        assert(line_to_replace->tags_cheri == 0);
        line_to_replace->dirty = false;
        line_to_replace->tag_addr = tag_addr;

        result = line_to_replace;
    }

    assert(result != NULL);

    // update cache line counters (LRU replacement policy)
    u16 result_counter = result->counter;

    if (result_counter != 0)
    {
        for (i64 i = 0; i < device->cache.num_ways; i++)
        {
            if (device->cache.entries[set_start_idx + i].counter <= result_counter)
            {
                device->cache.entries[set_start_idx + i].counter =
                    (device->cache.entries[set_start_idx + i].counter + 1) % device->cache.num_ways;
            }
        }

        result->counter = 0;
    }

    assert(result != NULL);
    assert(result->counter == 0);
}

static void update_unknown_tags(b8 * tags_cheri, b8 * tags_known, b8 new_tags_cheri, b8 new_tags_known)
{
    assert((*tags_cheri & ~(*tags_known)) == 0);
    assert((new_tags_cheri & ~(new_tags_known)) == 0);
    // TODO remove check? should be identical without
    if (*tags_known != new_tags_known)
    {
        assert((*tags_known | new_tags_known) == new_tags_known); // check is superset
        assert((*tags_cheri & *tags_known) == (new_tags_cheri & *tags_known)); // known bits unchanged

        *tags_cheri = new_tags_cheri;
        *tags_known = new_tags_known;
    }
    else
    {
        assert(*tags_cheri == new_tags_cheri);
    }
}

void device_write(device_t * device, u64 paddr, b8 tags_cheri, b8 tags_known)
{
    switch (device->type)
    {
        case DEVICE_TYPE_CACHE:
        {
            cache_write_back(device, paddr, tags_cheri, tags_known);
        } break;
        case DEVICE_TYPE_TAG_CACHE:
        {
            // TODO doesn't the tag cache need to know the tags? may want separate functions for that
            tag_cache_record_access(device, paddr); // TODO should record a write back, not a normal read access?

            tag_table_write(device->tag_cache.tags, device->tag_cache.tags_size, paddr, tags_cheri);
            tag_table_write(device->tag_cache.tags_known, device->tag_cache.tags_size, paddr, tags_known); // TODO
        } break;
        case DEVICE_TYPE_CONTROLLER_INTERFACE:
        {
            device->controller_interface.stats.writes++;
            controller_interface_write_entry(device, TAG_CACHE_REQUEST_TYPE_WRITE, paddr, tags_cheri, tags_known);

            tag_table_write(device->controller_interface.tags,
                device->controller_interface.tags_size, paddr, tags_cheri);
            tag_table_write(device->controller_interface.tags_known,
                device->controller_interface.tags_size, paddr, tags_known);

        } break;
        default: assert(!"Impossible.");
    }
}

void device_write_invisible(device_t * device, u64 paddr, b8 tags_cheri, b8 tags_known)
{
    switch (device->type)
    {
        case DEVICE_TYPE_CACHE:
        {
            cache_write_back_invisible(device, paddr, tags_cheri, tags_known);
        } break;
        case DEVICE_TYPE_CONTROLLER_INTERFACE:
        {
            b8 old_tags_cheri = tag_table_read(device->controller_interface.tags, device->controller_interface.tags_size, paddr);
            b8 old_tags_known = tag_table_read(device->controller_interface.tags_known, device->controller_interface.tags_size, paddr);

            // TODO this doesn't really make sense here. replace update_unknown_tags with a function that just does the checks?
            update_unknown_tags(&old_tags_cheri, &old_tags_known, tags_cheri, tags_known);

            assert(old_tags_known == tags_known);
            assert(old_tags_cheri == tags_cheri);

            tag_table_write(device->controller_interface.tags,
                device->controller_interface.tags_size, paddr, tags_cheri);
            tag_table_write(device->controller_interface.tags_known,
                device->controller_interface.tags_size, paddr, tags_known);
        } break;
        default: assert(!"Impossible.");
    }

    // /* NOTE while writing to the parent is not necessary for accuracy, it means that if a
    //  * CLOAD results in a miss in the LLC because it was the first time that data was read,
    //  * the output entry will contain the tag data from this CLOAD. */
    // if (device->parent)
    //     device_write_invisible(device->parent, paddr, tags_cheri, tags_known);
}

void device_read(device_t * device, u64 paddr, u8 * tags_cheri, u8 * tags_known)
{
    switch (device->type)
    {
        case DEVICE_TYPE_CACHE:
        {
            cache_line_t * cache_line = cache_request(device, paddr);

            *tags_cheri = cache_line->tags_cheri;
            *tags_known = cache_line->tags_known;
        } break;
        case DEVICE_TYPE_TAG_CACHE:
        {
            tag_cache_record_access(device, paddr);

            // TODO
            *tags_cheri = tag_table_read(device->tag_cache.tags, device->tag_cache.tags_size, paddr);
            *tags_known = tag_table_read(device->tag_cache.tags_known, device->tag_cache.tags_size, paddr);
        } break;
        case DEVICE_TYPE_CONTROLLER_INTERFACE:
        {
            device->controller_interface.stats.reads++;

            *tags_cheri = tag_table_read(device->controller_interface.tags, device->controller_interface.tags_size, paddr);
            *tags_known = tag_table_read(device->controller_interface.tags_known, device->controller_interface.tags_size, paddr);

            controller_interface_write_entry(device, TAG_CACHE_REQUEST_TYPE_READ, paddr, *tags_cheri, *tags_known);
        } break;
        default: assert(!"Impossible.");
    }
}


device_t * cache_init(arena_t * arena, char * name, u32 size_bytes, u32 num_ways, device_t * parent)
{
    // TODO
    device_t * device = arena_push(arena, sizeof(device_t));
    *device = (device_t) {0};
    device->type = DEVICE_TYPE_CACHE;

    assert(parent);
    device->parent = parent;
    assert(parent->num_children < DEVICE_MAX_CHILDREN);
    parent->children[parent->num_children++] = device;

    assert(size_bytes % CACHE_LINE_SIZE == 0);
    u32 size = size_bytes / CACHE_LINE_SIZE;

    device->cache.size = size;
    device->cache.num_ways = num_ways;
    assert(size % num_ways == 0);

    device->cache.entries = arena_push_array(arena, cache_line_t, size);

    for (i64 set_start_idx = 0; set_start_idx < size; set_start_idx += num_ways)
    for (i64 way = 0; way < num_ways; way++)
    {
        assert(set_start_idx + way < size);
        device->cache.entries[set_start_idx + way].tag_addr = INVALID_TAG;
        device->cache.entries[set_start_idx + way].counter = way; // LRU replacement policy
    }

    assert(name);
    device->cache.name = name;

    return device;
}

static inline i64 cache_find_existing(device_t * device, u64 tag_addr, u32 * set_start_idx_ptr)
{
    assert(device->type == DEVICE_TYPE_CACHE);

    u32 num_sets = device->cache.size / device->cache.num_ways;
    u32 set_start_idx = (tag_addr % num_sets) * device->cache.num_ways;

    assert(tag_addr != INVALID_TAG);

    assert(set_start_idx_ptr);
    *set_start_idx_ptr = set_start_idx;

    i64 way = -1;
    for (i64 i = 0; i < device->cache.num_ways; i++)
    {
        if (device->cache.entries[set_start_idx + i].tag_addr == tag_addr)
        {
            assert(way == -1);
            way = i;
        }
    }

    return way;
}

// TODO optional logging?

// TODO struct for tags_cheri and tags_known?

static void cache_write_back(device_t * device, u64 paddr, b8 tags_cheri, b8 tags_known)
{
    assert(device->type == DEVICE_TYPE_CACHE);
    assert(paddr % CACHE_LINE_SIZE == 0);

    u64 tag_addr = paddr >> CACHE_LINE_SIZE_BITS;
    u32 set_start_idx;
    i64 way = cache_find_existing(device, tag_addr, &set_start_idx);

    // since these caches are inclusive, we can assume that the cache line to overwrite already exists
    // (and no need to evict + write back to make space)
    assert(way >= 0);

    cache_line_t * result = &device->cache.entries[set_start_idx + way];
    assert(result->tag_addr != INVALID_TAG);
    assert(result->tag_addr == tag_addr);

    result->tags_cheri = tags_cheri;
    result->tags_known = tags_known;
    result->dirty = true;

    // NOTE assuming write backs do not affect replacement policy counters
}

static void cache_write_back_invisible(device_t * device, u64 paddr, b8 tags_cheri, b8 tags_known)
{
    assert(device->type == DEVICE_TYPE_CACHE);
    assert(paddr % CACHE_LINE_SIZE == 0);

    u64 tag_addr = paddr >> CACHE_LINE_SIZE_BITS;
    u32 set_start_idx;
    i64 way = cache_find_existing(device, tag_addr, &set_start_idx);

    // since these caches are inclusive, we can assume that the cache line to overwrite already exists
    // (and no need to forward invisible write back to next level in hierarchy)
    assert(way >= 0);

    cache_line_t * result = &device->cache.entries[set_start_idx + way];
    assert(result->tag_addr != INVALID_TAG);
    assert(result->tag_addr == tag_addr);

    update_unknown_tags(&result->tags_cheri, &result->tags_known, tags_cheri, tags_known);
}

cache_line_t * cache_request(device_t * device, u64 paddr)
{
    assert(device->type == DEVICE_TYPE_CACHE);
    assert(paddr % CACHE_LINE_SIZE == 0);

    // printf("cache request [ name: '%s', address: " FMT_ADDR " ]\n", device->cache.name, paddr);

    u64 tag_addr = paddr >> CACHE_LINE_SIZE_BITS;
    u32 set_start_idx;
    i64 way = cache_find_existing(device, tag_addr, &set_start_idx);

    cache_line_t * result = NULL;
    if (way >= 0)
    {
        device->cache.stats.hits++;

        result = &device->cache.entries[set_start_idx + way];

        // printf(INDENT4 "Hit [ found in way: %ld ]\n", way);
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

        // printf(INDENT4 "Miss [ replacing way: %lu ]\n", way);

        // printf(INDENT4 "Set contents:\n");
        // for (i64 i = 0; i < device->cache.num_ways; i++)
        // {
        //     cache_line_t cache_line = device->cache.entries[set_start_idx + i];
        //     printf(INDENT8 "Way %ld [ addr: %lu, counter: %hu, dirty: %s ]\n", i,
        //         cache_line.tag_addr != INVALID_TAG ? cache_line.tag_addr << CACHE_LINE_SIZE_BITS : 0,
        //         cache_line.counter,
        //         cache_line.dirty ? "true" : "false");
        // }

        assert(device->parent != NULL);

        cache_line_t * line_to_replace = &device->cache.entries[set_start_idx + way];

        if (line_to_replace->tag_addr != INVALID_TAG)
        {
            /* NOTE we perform back invalidations before evicting the cache line in case the children
             * have an updated copy of the data. This way, they write it back before the eviction. */

            // back invalidations
            u32 num_children_dirty = 0;
            for (i64 i = 0; i < device->num_children; i++)
            {
                device_t * child = device->children[i];
                assert(child->type == DEVICE_TYPE_CACHE);
                u32 child_set_start_idx;
                i64 child_way = cache_find_existing(child, line_to_replace->tag_addr, &child_set_start_idx);
                if (child_way >= 0)
                {
                    cache_line_t * child_cache_line = &child->cache.entries[child_set_start_idx + child_way];

                    assert(child->parent == device);
                    assert(child_cache_line->tag_addr != INVALID_TAG);
                    assert(child_cache_line->tag_addr == line_to_replace->tag_addr);

                    if (child_cache_line->dirty)
                    {
                        num_children_dirty++;

                        // write back from child to this cache
                        child->cache.stats.write_backs++;
                        cache_write_back(device, child_cache_line->tag_addr << CACHE_LINE_SIZE_BITS,
                            child_cache_line->tags_cheri, child_cache_line->tags_known);
                    }
                    else
                    {
                        cache_write_back_invisible(device, child_cache_line->tag_addr << CACHE_LINE_SIZE_BITS,
                            child_cache_line->tags_cheri, child_cache_line->tags_known);
                    }

                    child->cache.stats.invalidations++;
                    child_cache_line->tag_addr = INVALID_TAG;
                }
            }
            assert(num_children_dirty <= 1); // otherwise we have a coherence violation

            // evict if necessary
            if (line_to_replace->dirty)
            {
                // printf(INDENT8 "Writing-back cache line into parent device.\n");

                assert(line_to_replace->tag_addr != INVALID_TAG);

                device->cache.stats.write_backs++;
                device_write(device->parent, line_to_replace->tag_addr << CACHE_LINE_SIZE_BITS,
                    line_to_replace->tags_cheri, line_to_replace->tags_known);
            }
            else
            {
                device_write_invisible(device->parent, line_to_replace->tag_addr << CACHE_LINE_SIZE_BITS,
                    line_to_replace->tags_cheri, line_to_replace->tags_known);
            }
        }

        // forward read to next level cache
        b8 tags_cheri, tags_known;
        device_read(device->parent, paddr, &tags_cheri, &tags_known);

        assert(CACHE_LINE_SIZE % CAP_SIZE_BYTES == 0);
        assert(CACHE_LINE_SIZE / CAP_SIZE_BYTES <= 8);
        assert((~((1 << (CACHE_LINE_SIZE/CAP_SIZE_BYTES))-1) & tags_cheri) == 0);

        line_to_replace->dirty = false;
        line_to_replace->tag_addr = tag_addr;
        line_to_replace->tags_cheri = tags_cheri;
        line_to_replace->tags_known = tags_known;

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

    assert(result != NULL);

    // update cache line counters (LRU replacement policy)
    u16 result_counter = result->counter;

    if (result_counter != 0)
    {
        for (i64 i = 0; i < device->cache.num_ways; i++)
        {
            if (device->cache.entries[set_start_idx + i].counter <= result_counter)
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


static char * device_get_name(device_t * device)
{
    switch (device->type)
    {
        case DEVICE_TYPE_CACHE:
        {
            assert(device->cache.name);
            return device->cache.name;
        } break;
        case DEVICE_TYPE_TAG_CACHE:
        {
            return "TAG CONTROLLER (UNCOMPRESSED)"; // TODO compressed/other kinds?
        } break;
        case DEVICE_TYPE_CONTROLLER_INTERFACE:
        {
            return "TAG CONTROLLER (INTERFACE)";
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
                device_get_name(device), device->cache.num_ways, device->cache.size * CACHE_LINE_SIZE,
                device->parent ? device_get_name(device->parent) : "NULL");
        } break;
        case DEVICE_TYPE_TAG_CACHE:
        {
            printf(INDENT4 "%s (%u-way, %u bytes)\n",
                device_get_name(device), device->tag_cache.num_ways, device->tag_cache.size * CACHE_LINE_SIZE);
        } break;
        case DEVICE_TYPE_CONTROLLER_INTERFACE:
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
            printf(INDENT8 "Invalidations: %lu\n", device->cache.stats.invalidations);
            printf(INDENT8 "Write backs: %lu\n", device->cache.stats.write_backs);

            double miss_rate =
                (double) device->cache.stats.misses / (device->cache.stats.hits + device->cache.stats.misses);
            printf(INDENT8 "Miss rate: %.2f%%\n", miss_rate * 100);
        } break;
        case DEVICE_TYPE_TAG_CACHE:
        {
            printf(INDENT8 "Hits: %lu\n", device->tag_cache.stats.hits);
            printf(INDENT8 "Misses: %lu\n", device->tag_cache.stats.misses);
            printf(INDENT8 "Write backs: %lu\n", device->tag_cache.stats.write_backs);

            double miss_rate =
                (double) device->tag_cache.stats.misses / (device->tag_cache.stats.hits + device->tag_cache.stats.misses);
            printf(INDENT8 "Miss rate: %.2f%%\n", miss_rate * 100);
        } break;
        case DEVICE_TYPE_CONTROLLER_INTERFACE:
        {
            printf(INDENT8 "Reads: %lu\n", device->controller_interface.stats.reads);
            printf(INDENT8 "Writes: %lu\n", device->controller_interface.stats.writes);
        } break;
        default: assert(!"Impossible.");
    }
}

void device_cleanup(device_t * device)
{
    switch (device->type)
    {
        case DEVICE_TYPE_CONTROLLER_INTERFACE:
        {
            controller_interface_cleanup(device);
        } break;
        case DEVICE_TYPE_CACHE:
        case DEVICE_TYPE_TAG_CACHE:
            break;
        default: assert(!"Impossible.");
    }
}
