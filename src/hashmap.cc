#include <unordered_map>
#include <unordered_set>

extern "C"
{
	#include "hashmap.h"
}

#include <assert.h>


extern "C" map_u64 map_u64_create(void)
{
	map_u64 map = {0};
	map.ptr = new std::unordered_map<u64,u64>();
	return map;
}

extern "C" void map_u64_cleanup(map_u64 * map)
{
	assert(map->ptr);
	std::unordered_map<u64,u64> * map_ptr = (std::unordered_map<u64,u64> *) map->ptr;
	delete map_ptr;
	map->ptr = nullptr;
}

extern "C" bool map_u64_get(map_u64 map, u64 key, u64 * value)
{
	assert(map.ptr);
	std::unordered_map<u64,u64> * map_ptr = (std::unordered_map<u64,u64> *) map.ptr;
	auto entry = map_ptr->find(key);
	if (entry != map_ptr->end())
	{
		*value = entry->second;
		return true;
	}

	return false;
}

extern "C" void map_u64_set(map_u64 map, u64 key, u64 value)
{
	assert(map.ptr);
	std::unordered_map<u64,u64> * map_ptr = (std::unordered_map<u64,u64> *) map.ptr;
	(*map_ptr)[key] = value;
}


extern "C" set_u64 set_u64_create(void)
{
	set_u64 set = {0};
	set.ptr = new std::unordered_set<u64>();
	return set;
}

extern "C" void set_u64_cleanup(set_u64 * set)
{
	assert(set->ptr);
	std::unordered_set<u64> * set_ptr = (std::unordered_set<u64> *) set->ptr;
	delete set_ptr;
	set->ptr = nullptr;
}

extern "C" void set_u64_insert(set_u64 set, u64 key)
{
	assert(set.ptr);
	std::unordered_set<u64> * set_ptr = (std::unordered_set<u64> *) set.ptr;
	set_ptr->insert(key);
}

extern "C" bool set_u64_contains(set_u64 set, u64 key)
{
	assert(set.ptr);
	std::unordered_set<u64> * set_ptr = (std::unordered_set<u64> *) set.ptr;
	return set_ptr->find(key) != set_ptr->end();
}

extern "C" u64 set_u64_size(set_u64 set)
{
	assert(set.ptr);
	std::unordered_set<u64> * set_ptr = (std::unordered_set<u64> *) set.ptr;
	return (u64) set_ptr->size();
}
