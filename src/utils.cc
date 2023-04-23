#include "utils.h"

#include <stdio.h>
#include <stdlib.h>

void quit(void)
{
    printf("Quitting.\n");
    exit(1);
}

bool file_exists(char * filename)
{
    FILE * fd = fopen(filename, "rb");
    if (fd) fclose(fd);
    return fd != NULL;
}
