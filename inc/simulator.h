#ifndef SIMULATOR_INCLUDE
#define SIMULATOR_INCLUDE

#include "jdp.h"
#include "io.h"

#define CACHE_LINE_SIZE 64
#define CACHE_LINE_SIZE_BITS 6
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
    u64 write_backs;
    u64 invalidations;
    // TODO types of misses?
    // TODO prefetcher?
};

typedef struct tag_cache_stats_t tag_cache_stats_t;
struct tag_cache_stats_t
{
	u64 hits;
	u64 misses;
    u64 write_backs;
};

typedef struct controller_interface_stats_t controller_interface_stats_t;
struct controller_interface_stats_t
{
	u64 reads;
	u64 writes;
	// TODO reads_untagged?
	// TODO writes_untagged?
};

typedef struct tags_t tags_t;
struct tags_t
{
    // TODO make 16 bits to support larger cache line sizes (>128)?
    b8 data;
    b8 known;
};

// for the L1/L2/L3 caches
typedef struct cache_line_t cache_line_t;
struct cache_line_t
{
    u64 tag_addr;
    u16 counter;
    tags_t tags_cheri;
    bool dirty;
};

typedef struct cache_t cache_t;
struct cache_t
{
    u32 size;
    u32 num_ways;
    cache_line_t * entries;
    cache_stats_t stats;
    char * name;
};

typedef struct tag_table_t tag_table_t;
struct tag_table_t
{
    u8 * data;
    u8 * known;
    u32 size;
};

typedef struct tag_cache_t tag_cache_t;
struct tag_cache_t
{
    u32 size;
    u32 num_ways;
    cache_line_t * entries; // NOTE won't store tag in them
    // NOTE the tag table here reflects the controller's view of memory
    tag_table_t tag_table;
    tag_cache_stats_t stats;
};

typedef struct controller_interface_t controller_interface_t;
struct controller_interface_t
{
	trace_writer_t output;
    // NOTE the tag table here reflects the controller's view of memory
    tag_table_t tag_table;
    controller_interface_stats_t stats;
};

enum device_type_t
{
    DEVICE_TYPE_CACHE,
    DEVICE_TYPE_TAG_CACHE,
    DEVICE_TYPE_CONTROLLER_INTERFACE
};

#define DEVICE_MAX_CHILDREN 2

typedef struct device_t device_t;
struct device_t
{
    u8 type;
    device_t * parent;
    union
    {
        cache_t cache;
        tag_cache_t tag_cache;
        controller_interface_t controller_interface;
    };
    device_t * children[DEVICE_MAX_CHILDREN];
    u32 num_children;
};

cache_line_t * cache_request(device_t * device, u64 paddr);
device_t * cache_init(arena_t * arena, char * name, u32 size, u32 num_ways, device_t * parent);

device_t * tag_cache_init(arena_t * arena, char * initial_tags_filename, u32 size, u32 num_ways);
device_t * controller_interface_init(arena_t * arena, char * output_filename);

void device_write(device_t * device, u64 paddr, tags_t tags_cheri);
tags_t device_read(device_t * device, u64 paddr);

void notify_peers_coherence_flush(device_t * device, u64 paddr, bool should_invalidate);

void device_print_configuration(device_t * device);
void device_print_statistics(device_t * device);
void device_cleanup(device_t * device);

#endif /* SIMULATOR_INCLUDE */
