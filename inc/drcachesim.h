#ifndef DRCACHESIM_INCLUDE
#define DRCACHESIM_INCLUDE

#include <zlib.h>
#include "trace.h"
#include "hashmap.h"
#include "io.h"

void write_drcachesim_header(trace_writer * writer);
void write_drcachesim_footer(trace_writer * writer);
void write_drcachesim_trace_entry_vaddr(trace_writer * writer, map_u64 page_table, custom_trace_entry_t custom_entry);
void write_drcachesim_trace_entry_paddr(trace_writer * writer, custom_trace_entry_t custom_entry);

#endif /* DRCACHESIM_INCLUDE */
