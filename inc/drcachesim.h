#ifndef DRCACHESIM_INCLUDE
#define DRCACHESIM_INCLUDE

#include <zlib.h>
#include "trace.h"

void write_drcachesim_header(gzFile file);
void write_drcachesim_trace_entry(gzFile file, custom_trace_entry_t custom_entry);
void write_drcachesim_footer(gzFile file);

#endif /* DRCACHESIM_INCLUDE */