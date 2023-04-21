#ifndef SIMULATOR_INCLUDE
#define SIMULATOR_INCLUDE

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

// // TODO do stats properly
// typedef struct simulator_stats_t simulator_stats_t;
// struct simulator_stats_t
// {
// 	// TODO
// };
extern u64 dbg_dram_writes;
extern u64 dbg_dram_reads;

cache_line_t * cache_lookup(device_t * device, u64 paddr);
device_t cache_init(arena_t * arena, u32 size, u32 num_ways, device_t * parent);
device_t tag_cache_init(arena_t * arena);

#endif /* SIMULATOR_INCLUDE */
