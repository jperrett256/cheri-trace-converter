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
    string_t description;
};

void print_commands_and_quit(char * exe_name, command_t * commands, u32 num_commands)
{
    printf("Usage: %s <command> [<argument1> ...]\n", exe_name);
    printf("\n");
    printf("Available commands:\n");
    for (i64 i = 0; i < num_commands; i++)
    {
        printf(INDENT4 "%.*s\n", string_varg(commands[i].name));
        printf(INDENT8 "%.*s\n", string_varg(commands[i].description));
    }

    printf("\n");
    printf("Run a command without any arguments for usage information.\n");
    printf("\n");
    quit();
}

// static void handler_missing(COMMAND_HANDLER_ARGS)
// {
//     assert(!"Handler not implemented!");
// }

int main(int argc, char * argv[])
{
    arena_t arena = arena_alloc(MEGABYTES(512));

    command_t commands[] =
    {
        {
            string_lit("get-info"),
            trace_get_info,
            string_lit("Prints statistics about a trace.")
        },
        {
            string_lit("patch-paddrs"),
            trace_patch_paddrs,
            string_lit("Patching missing / invalid physical addresses using previous instructions.")
        },
        {
            string_lit("convert"),
            trace_convert,
            string_lit("Converts standard QEMU-CHERI traces from one compression type to another.")
        },
        {
            string_lit("convert-generic"),
            trace_convert_generic,
            string_lit("Converts any file from one compression type to another (works for drcachesim / LLC outgoing request traces).")
        },
        {
            string_lit("split"),
            trace_split,
            string_lit("Reads in an input trace and writes two out (somewhat like tee).")
        },
        {
            string_lit("convert-drcachesim-vaddr"),
            trace_convert_drcachesim_vaddr,
            string_lit("Converts standard traces to the drcachesim trace format, containing virtual addresses with physical mapping entries.")
        },
        {
            string_lit("convert-drcachesim-paddr"),
            trace_convert_drcachesim_paddr,
            string_lit("Converts standard traces to the drcachesim trace format, containing only physical addresses (in the place of virtual ones).")
        },
        {
            string_lit("get-initial-state"),
            trace_get_initial_accesses,
            string_lit("This creates a file indicating the first access type to every memory location (for reconstructing the initial tag state in memory).")
        },
        {
            string_lit("simulate"),
            trace_simulate,
            string_lit("Simulates instruction and data caches, outputs the outgoing requests from the LLC.")
        },
        {
            string_lit("simulate-tag-cache"),
            trace_simulate_uncompressed,
            string_lit("Simulates a tag cache without compression, given a trace of the outgoing requests from the LLC.")
        },
        {
            string_lit("requests-get-info"),
            trace_requests_get_info,
            string_lit("Displays information about a file containing outgoing requests from the LLC.")
        },
        {
            string_lit("requests-tag-csv"),
            trace_requests_make_tag_csv,
            string_lit("Reads in an LLC outgoing requests trace, outputs to stdout (in CSV form) information about how tags change over time.")
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
