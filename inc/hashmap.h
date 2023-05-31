#ifndef HASHMAP_INCLUDE
#define HASHMAP_INCLUDE

#include "jdp.h"
#include <stdbool.h>

typedef struct map_u64 map_u64;
struct map_u64
{
	void * ptr;
};

typedef struct set_u64 set_u64;
struct set_u64
{
	void * ptr;
};

map_u64 map_u64_create(void);
void map_u64_cleanup(map_u64 * map);
bool map_u64_get(map_u64 map, u64 key, u64 * value);
void map_u64_set(map_u64 map, u64 key, u64 value);

set_u64 set_u64_create(void);
void set_u64_cleanup(set_u64 * set);
void set_u64_insert(set_u64 set, u64 key);
void set_u64_remove(set_u64 set, u64 key);
bool set_u64_contains(set_u64 set, u64 key);
u64 set_u64_size(set_u64 set);

#endif /* HASHMAP_INCLUDE */
