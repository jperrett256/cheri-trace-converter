#ifndef SIMULATOR_INCLUDE
#define SIMULATOR_INCLUDE

#include "jdp.h"
#include <zlib.h>

#define CACHE_LINE_SIZE 128
#define CACHE_LINE_SIZE_BITS 7
static_assert((1 << CACHE_LINE_SIZE_BITS) == CACHE_LINE_SIZE, "Cache line size constants are incorrect.");

#define INVALID_TAG ((u64) -1)

/*
Crucial points:
- Treating the caches above the tag controller (L1, L2, etc.) differently from the tag controller
- Because the behaviour of the tag cache depends on the cache contents, we need to maintain a view of the tag state from the perspective of the tag controller.
- Therefore, we have:
	- A simple buffer containing the entire tag state in memory, but from the view of the tag controller
	- The cache lines in the caches above the tag controller store tags, and write them back into upper caches / tag buffer as required (write-back cache)
*/

typedef struct cache_stats_t cache_stats_t;
struct cache_stats_t
{
	u64 hits;
	u64 misses;
	// TODO types of misses?
	// TODO prefetcher?
};

typedef struct controller_interface_stats_t controller_interface_stats_t;
struct controller_interface_stats_t
{
	u64 reads;
	u64 writes;
	// TODO reads_untagged?
	// TODO writes_untagged?
};

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
    cache_stats_t stats;
    char * name;
};

// TODO
// typedef struct tag_cache_t tag_cache_t;
// struct tag_cache_t
// {
//     cache_line_t * entries; // NOTE won't store tag in them
//     u32 tags_size;
//     u8 * tags; // NOTE tag controller's view of memory
//     tag_cache_stats_t stats;
// };

typedef struct controller_interface_t controller_interface_t;
struct controller_interface_t
{
	gzFile output;
    u32 tags_size;
    u8 * tags; // NOTE tag controller's view of memory
    controller_interface_stats_t stats;
};

enum device_type_t
{
    DEVICE_TYPE_CACHE,
    // DEVICE_TYPE_TAG_CACHE // TODO
    DEVICE_TYPE_CONTROLLER_INTERFACE
};

typedef struct device_t device_t;
struct device_t
{
    u8 type;
    device_t * parent;
    union
    {
        cache_t cache;
        // tag_cache_t tag_cache;
        controller_interface_t controller_interface;
    };
};

cache_line_t * cache_lookup(device_t * device, u64 paddr);
device_t cache_init(arena_t * arena, const char * name, u32 size, u32 num_ways, device_t * parent);

device_t tag_cache_init(arena_t * arena, char * initial_tags_filename);
device_t controller_interface_init(arena_t * arena, char * initial_tags_filename, char * output_filename);

void device_print_configuration(device_t * device);
void device_print_statistics(device_t * device);
void device_cleanup(device_t * device);

#endif /* SIMULATOR_INCLUDE */
