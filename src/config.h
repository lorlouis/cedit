#ifndef CONFIG_H
#define CONFIG_H 1

#include <stdbool.h>

struct config {
    int tab_width;
    bool use_spaces;
    const char *copy_command;
    const char *paste_command;
};

extern const struct config CONFIG;

#endif

