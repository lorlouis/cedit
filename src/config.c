#include "config.h"

const struct config CONFIG = {
    .tab_width = 4,
    .use_spaces = true,
#ifdef __MACH__
    .copy_command = "pbcopy",
    .paste_command = "pbpaste",
#elifdef XORG
    .copy_command = "xsel -b -i",
    .paste_command = "xsel -b -o",
#else
    .copy_command = "wl-copy -p",
    .paste_command = "wl-paste -p",
#endif
    .poll_delay = 25000,
};

#ifdef TESTING

#include "tests.h"

TESTS_START

TESTS_END

#endif
