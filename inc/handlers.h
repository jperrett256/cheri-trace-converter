#ifndef HANDLERS_INCLUDE
#define HANDLERS_INCLUDE

#include "jdp.h"

#define COMMAND_HANDLER_ARGS arena_t * arena, char * exe_name, char * cmd_name, int num_args, char ** args

void trace_get_info(COMMAND_HANDLER_ARGS);
void trace_patch_paddrs(COMMAND_HANDLER_ARGS);
void trace_convert(COMMAND_HANDLER_ARGS);
void trace_convert_generic(COMMAND_HANDLER_ARGS);
void trace_split(COMMAND_HANDLER_ARGS);
void trace_convert_drcachesim_vaddr(COMMAND_HANDLER_ARGS);
void trace_convert_drcachesim_paddr(COMMAND_HANDLER_ARGS);
void trace_get_initial_accesses(COMMAND_HANDLER_ARGS);
void trace_simulate(COMMAND_HANDLER_ARGS);
void trace_simulate_uncompressed(COMMAND_HANDLER_ARGS);
void trace_requests_get_info(COMMAND_HANDLER_ARGS);
void trace_requests_make_tag_csv(COMMAND_HANDLER_ARGS);

#endif /* HANDLERS_INCLUDE */
