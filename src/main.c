#include "utils.h"
#include "handlers.h"

#include <stdio.h>
#include <assert.h>

#define JDP_IMPLEMENTATION
#include "jdp.h"
#undef JDP_IMPLEMENTATION

typedef struct command_t command_t;
struct command_t
{
    string_t name;
    void (*handler)(COMMAND_HANDLER_ARGS);
};

void print_commands_and_quit(char * exe_name, command_t * commands, u32 num_commands)
{
    printf("Usage: %s <command> [<argument1> ...]\n", exe_name);
    printf("Available commands:\n");
    for (i64 i = 0; i < num_commands; i++)
    {
        printf("    %.*s\n", string_varg(commands[i].name));
    }
    quit();
}

// static void handler_missing(COMMAND_HANDLER_ARGS)
// {
//     assert(!"Handler not implemented!");
// }

int main(int argc, char * argv[])
{
    arena_t arena = arena_alloc(MEGABYTES(64));

    // TODO add help text?
    command_t commands[] =
    {
        {
            string_lit("get-info"),
            trace_get_info
        },
        {
            string_lit("patch-paddrs"),
            trace_patch_paddrs
            // TODO if you wanna implement your own hashmap, checking the output of this with previous results would be a good way to test it
        },
        {
            string_lit("convert"), /* TODO eventually call "convert" and just use file extension */
            trace_convert
        },
        {
            string_lit("convert-drcachesim-vaddr"),
            trace_convert_drcachesim_vaddr
        },
        {
            string_lit("convert-drcachesim-paddr"),
            trace_convert_drcachesim_paddr
        },
        {
            string_lit("get-initial-tags"),
            trace_get_initial_tags
        },
        {
            string_lit("simulate"),
            trace_simulate
        },
        {
            string_lit("simulate-tag-cache"),
            trace_simulate_uncompressed
        }
    };

    assert(array_count(commands) <= UINT32_MAX);
    u32 num_commands = (u32) array_count(commands);

    if (argc < 2)
    {
        print_commands_and_quit(argv[0], commands, num_commands);
    }

    string_t command = string_from_cstr(argv[1]);
    i64 match_index = -1;
    for (i64 i = 0; i < num_commands; i++)
    {
        if (string_match(commands[i].name, command))
        {
            match_index = i;
            break;
        }
    }

    if (match_index >= 0)
    {
        assert(match_index < num_commands);
        commands[match_index].handler(&arena, argv[0], argv[1], argc - 2, &argv[2]);
    }
    else
    {
        print_commands_and_quit(argv[0], commands, num_commands);
    }

    arena_free(&arena);

    return 0;
}
