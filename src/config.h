#ifndef CONFIG_H
#define CONFIG_H 1

#include <stdbool.h>
#include <unistd.h>

struct config {
    int tab_width;
    bool use_spaces;
    const char *copy_command;
    const char *paste_command;
    const useconds_t poll_delay;
};

extern const struct config CONFIG;

#endif

