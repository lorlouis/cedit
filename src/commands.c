#include "editor.h"

#include <assert.h>
#include <string.h>

int exec_command(char *command) {
    assert(*command == ':' && ": is required at the start of a command");
    char *sep = " ";
    char *token = strtok(command+1, " ");
    int no_confirm = 0;

    if(!token) return 0;

    int command_len = strlen(token);
    if(command_len > 1 && token[command_len-1] == '!') {
        token[command_len-1] = '\0';
        no_confirm = 1;
    }

    if(!strcmp(token, "q")) {
        editor_quit(no_confirm);
        return 0;
    } else if(!strcmp(token, "qa")) {
        RUNNING = 0;
        return 0;
    } else if(!strcmp(token, "w")) {
        // a null token is fine
        token = strtok(NULL, sep);
        editor_write(token);
        return 0;
    } else if(!strcmp(token, "wq")) {
        // a null token is fine
        token = strtok(NULL, sep);
        editor_write(token);

        editor_quit(no_confirm);
        return 0;
    } else if(!strcmp(token, "e")) {
        // a null token is fine
        token = strtok(NULL, sep);
        editor_open(token, FM_RW, no_confirm);
        return 0;
    } else if(!strcmp(token, "tabnew")) {
        // a null token is fine
        token = strtok(NULL, sep);
        editor_tabnew(token, FM_RW);
    } else if(!strcmp(token, "split")) {
        // a null token is fine
        token = strtok(NULL, sep);
        editor_split_open(token, FM_RW, SD_Vertical);
    } else if(!strcmp(token, "hsplit")) {
        // a null token is fine
        token = strtok(NULL, sep);
        editor_split_open(token, FM_RW, SD_Horizontal);
    } else {
        message_print("E: unknown command");
    }
    return 0;
}
