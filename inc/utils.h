#ifndef UTILS_INCLUDE
#define UTILS_INCLUDE

#include <stdbool.h>

void quit(void);
bool file_exists(char * filename);
bool confirm_overwrite_file(char * filename);

#endif /* UTILS_INCLUDE */
