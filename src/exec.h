#ifndef EXEC_H
#define EXEC_H 1

#include <sys/types.h>
#include "str.h"

typedef struct {
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
    pid_t pid;
} SpawnHandle;

void spawn_handle_free(SpawnHandle *handle);

int spawn_handle_wait_collect_output(SpawnHandle *handle, Str *out);

int spawn_captured(const char *command, SpawnHandle *spawn_handle);

#endif
