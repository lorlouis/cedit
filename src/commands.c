#include <string.h>
#include "editor.h"

int exec_command(char *command) {
    char *last = 0;
    char *sep = " ";
    char *token = strtok_r(command, ":", &last);
    do {
        if(!token) break;
        if(!strcmp(token, "q")) {
            editor_quit();
            return 0;
        } else if(!strcmp(token, "qa")) {
            RUNNING = 0;
            return 0;
        } else {
            message_print("E: unknown command");
        }
    } while((token = strtok_r(NULL, sep, &last)));
    return 0;
}
