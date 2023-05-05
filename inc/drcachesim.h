#ifndef DRCACHESIM_INCLUDE
#define DRCACHESIM_INCLUDE

#include <zlib.h>
#include "trace.h"
#include "hashmap.h"

void write_drcachesim_header(gzFile file);
void write_drcachesim_footer(gzFile file);
void write_drcachesim_trace_entry_vaddr(gzFile file, map_u64 page_table, custom_trace_entry_t custom_entry);
void write_drcachesim_trace_entry_paddr(gzFile file, custom_trace_entry_t custom_entry);

#endif /* DRCACHESIM_INCLUDE */
