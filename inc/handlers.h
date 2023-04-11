#ifndef HANDLERS_INCLUDE
#define HANDLERS_INCLUDE

#include "jdp.h"

#define COMMAND_HANDLER_ARGS arena_t * arena, char * exe_name, char * cmd_name, int num_args, char ** args

void trace_get_info(COMMAND_HANDLER_ARGS);
void trace_patch_paddrs(COMMAND_HANDLER_ARGS);
void trace_get_initial_tags(COMMAND_HANDLER_ARGS);
void trace_convert(COMMAND_HANDLER_ARGS);
void trace_simulate(COMMAND_HANDLER_ARGS);

#endif /* HANDLERS_INCLUDE */
