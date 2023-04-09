#include "common.h"
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

int main(int argc, char * argv[])
{
    arena_t arena = arena_alloc(MEGABYTES(64));

    command_t commands[] =
    {
        {
            string_lit("get-info"),
            tracesim_get_info
        },
        {
            string_lit("patch-paddrs"),
            tracesim_patch_paddrs
        },
        {
            string_lit("convert"), /* TODO eventually call "convert" and just use file extension */
            NULL // TODO
        },
        {
            string_lit("simulate"),
            NULL // TODO
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
