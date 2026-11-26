#ifndef AI_SYNC_GIT_PROCESS_H
#define AI_SYNC_GIT_PROCESS_H

#include <stddef.h>

int sync_git_run(const char *cwd, const char *const *options, char *result, size_t cap);
int sync_git_read_text(const char *cwd, const char *const *options, char *result, size_t cap);

#endif
