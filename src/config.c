#include "config.h"

const struct config CONFIG = {
    .tab_width = 4,
    .use_spaces = true,
#ifdef __MACH__
    .copy_command = "pbcopy",
    .paste_command = "pbpaste",
#else
    .copy_command = "xclip -i",
    .paste_command = "xclip -o",
#endif
};

