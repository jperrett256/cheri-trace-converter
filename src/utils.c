#include "utils.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#include <sys/stat.h>
#include <unistd.h>

void quit(void)
{
    printf("Quitting.\n");
    exit(1);
}

bool file_exists_not_fifo(char * filename)
{
    struct stat buf;
    return stat(filename, &buf) == 0 && !S_ISFIFO(buf.st_mode);
}

bool confirm_overwrite_file(char * filename)
{
    assert(file_exists_not_fifo(filename));

    bool result = false;
    while (true)
    {
        fprintf(stderr, "Overwrite existing file \"%s\"? [y/N] ", filename);
        int c = fgetc(stdin);
        result = (c == 'y' || c == 'Y');

        if (c == '\n' || c == EOF) break;

        int64_t num_extra = 0;
        while ((c = fgetc(stdin)) != '\n' && c != EOF) num_extra++;
        if (num_extra == 0) break;
    }

    return result;
}
