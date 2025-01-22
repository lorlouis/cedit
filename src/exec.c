#include "exec.h"
#include "str.h"
#include "xalloc.h"

#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <spawn.h>
#include <sys/wait.h>

// access this process' env
extern char **environ;

void spawn_handle_free(SpawnHandle *handle) {
    close(handle->stdin_fd);
    close(handle->stdout_fd);
    close(handle->stderr_fd);
}

int spawn_handle_wait_collect_output(SpawnHandle *handle, Str *out) {
    #define BUFFER_SIZE 128
    char buffer[BUFFER_SIZE];
    int stact_lock = 0;
    int ret;

    close(handle->stdin_fd);

    int fds_len = 2;
    struct pollfd fds[2] = {0};
    // put stderr first in the list to collect its output first
    fds[0].fd = handle->stderr_fd;
    fds[0].events = POLLIN;

    fds[1].fd = handle->stdout_fd;
    fds[1].events = POLLIN;

    do {
        ret = poll(fds, fds_len, 10);
        while(ret > 0) {
            for(int i = 0; i < fds_len; i++) {
                if(fds[i].revents & POLLIN) {
                    ret = read(fds[i].fd, buffer, BUFFER_SIZE);
                    if(ret < 0) return -1;
                    str_push(out, buffer, ret);
                } else if(fds[i].revents & POLLNVAL) {
                    return -1;
                } else if(fds[i].revents & POLLHUP) {
                    // WARN(louis) this code assumes there are only 2 fds
                    if(i == 0) {
                        fds[0] = fds[1];
                        i = -1;
                    }
                    fds_len -= 1;
                }
            }
            ret = poll(fds, fds_len, 10);
        }
    } while(!waitpid(handle->pid, &stact_lock, WNOHANG));
    return 0;
}

int spawn_captured(const char *command, SpawnHandle *spawn_handle) {
    int pipdes[2] = {0};
    pid_t child_pid = 0;

    Vec args = VEC_NEW(char*, 0);
    // strtok modifies the string
    size_t command_size = strlen(command)+1;
    char *command_buffer = xmalloc(command_size);
    memcpy(command_buffer, command, command_size);

    char *token = strtok(command_buffer, " ");
    while(token) {
        vec_push(&args, &token);
        token = strtok(0, " ");
    }

    if(args.len == 0) {
        xfree(command_buffer);
        return -1;
    }
    char *null = NULL;
    vec_push(&args, &null);

    if(pipe(pipdes)) {
        return -1;
    }

    int stdin_out = pipdes[0];
    int stdin_in = pipdes[1];

    if(pipe(pipdes)) {
        close(stdin_out);
        close(stdin_in);
        return -1;
    }

    int stdout_out = pipdes[0];
    int stdout_in = pipdes[1];

    if(pipe(pipdes)) {
        close(stdin_out);
        close(stdin_in);
        close(stdout_out);
        close(stdout_in);
        return -1;
    }

    int stderr_out = pipdes[0];
    int stderr_in = pipdes[1];

    posix_spawn_file_actions_t file_actions;
    posix_spawn_file_actions_init(&file_actions);

    // replace stdin
    posix_spawn_file_actions_addclose(&file_actions, STDIN_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, stdin_in);
    posix_spawn_file_actions_adddup2(&file_actions, stdin_out, STDIN_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, stdin_out);

    // replace stdout
    posix_spawn_file_actions_addclose(&file_actions, STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, stdout_out);
    posix_spawn_file_actions_adddup2(&file_actions, stdout_in, STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, stdout_in);

    // replace stderr
    posix_spawn_file_actions_addclose(&file_actions, STDERR_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, stderr_out);
    posix_spawn_file_actions_adddup2(&file_actions, stderr_in, STDERR_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, stderr_in);

    posix_spawnattr_t attrp;
    posix_spawnattr_init(&attrp);

    int res = posix_spawnp(
            &child_pid,
            *VEC_GET(char*, &args, 0),
            &file_actions, &attrp,
            VEC_GET(char*, &args, 0),
            environ);

    // cleanup resources
    posix_spawn_file_actions_destroy(&file_actions);
    posix_spawnattr_destroy(&attrp);
    xfree(command_buffer);
    vec_cleanup(&args);

    close(stdin_out);
    close(stdout_in);
    close(stderr_in);

    if(res) return res;

    // set the out parameter
    spawn_handle->pid = child_pid;
    spawn_handle->stdin_fd = stdin_in;
    spawn_handle->stdout_fd = stdout_out;
    spawn_handle->stderr_fd = stderr_out;
    return 0;
}

#ifdef TESTING

#include "tests.h"

TESTS_START

TEST_DEF(test_hello)
	ASSERT(1);
TEST_ENDDEF

TESTS_END

#endif
