#include "editor.h"

#include <assert.h>
#include <string.h>

int exec_command(char *command) {
    assert(*command == ':' && ": is required at the start of a command");
    char *sep = " ";
    char *token = strtok(command+1, " ");

    if(!token) return 0;

    if(!strcmp(token, "q")) {
        editor_quit();
        return 0;
    } else if(!strcmp(token, "qa")) {
        RUNNING = 0;
        return 0;
    } else if(!strcmp(token, "w")) {
        token = strtok(NULL, sep);
        editor_write(token);
        return 0;
    }

    message_print("E: unknown command");
    return 0;
}
