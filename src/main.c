#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/termios.h>
#include <string.h>

#include "termkey.h"
#include "vt.h"
#include "editor.h"
#include "utf.h"

struct termios INITIAL = {0};

void term_restore(void) {
    if(tcsetattr(STDOUT_FILENO, TCSANOW, &INITIAL)) {
        perror("unable to restore termio state");
    }
}

void term_init(void) {
    struct termios term_state;
    if(tcgetattr(STDOUT_FILENO, &term_state)) {
        perror("unable to get termio state");
        exit(1);
    }
    if(atexit(term_restore)) {
        perror("unable to register atexit(term_restore)");
        exit(1);
    }

    // save current state
    INITIAL = term_state;

    // don't echo back
    // and turn on immediate mode
    term_state.c_lflag &= (~ECHO & ~ICANON);
    term_state.c_cc[VTIME] = 0;
    term_state.c_cc[VMIN] = 0;

    if(tcsetattr(STDOUT_FILENO, TCSANOW, &term_state)) {
        perror("unable to initialise terminal");
        exit(1);
    }
    return;
}

void cleanup(void) {
    dprintf(STDOUT_FILENO, CUR_SHOW);
    exit(0);
}

void cleanup_err(int _i __attribute__((unused))) {
    dprintf(STDOUT_FILENO, CUR_SHOW);
    // this is not safe in a signal handler
    exit(1);
}

// i parameter is ignored
void on_resize(int i) {
    (void)i;
    int winsize_call = TIOCGWINSZ;
    int res = ioctl(STDOUT_FILENO, winsize_call, &WS);
    if(-1 == res) {
        perror("unable to resize window");
    }
}

int handle_keys(void) {
    struct KeyEvent e = {0};
    int had_key = 0;
    int ret = 0;
    while((ret = readkey(STDIN_FILENO, &e) > 0)) {
        had_key = 1;
        struct ModeInterface mode = mode_current();
        mode.handle_key(&e);
        memset(&e, 0, sizeof(struct KeyEvent));
    }
    if(ret == -1) return -1;
    return had_key;
    /*
    char buffer[32] = {0};
    struct KeyEvent e = {0};
    int had_key = 0;
    int ret = 0;
    while((ret = readkey(STDIN_FILENO, &e) > 0)) {
        had_key = 1;

        keyevent_fmt(&e, buffer, 32);
        printf("%s\n", buffer);

        memset(&e, 0, sizeof(struct KeyEvent));
        memset(&buffer, 0, 32);
    }
    if(ret == -1) return -1;
    return had_key;
    */
}

int main(int argc, const char **argv) {
    // parse options
    load_locale();

    // register cleanup
    if(atexit(cleanup)) {
        perror("unable to register atexit hook");
        exit(1);
    }
    on_resize(0);
    // some terminals, or applications (like lldb)
    // do not handle ioctl properly
    if(WS.ws_col == 0 && WS.ws_row == 0) {
        /*
        fprintf(stderr,
                "the current terminal did not "
                "handle ioctl resize properly, bailing out");
        exit(1);
        */
        WS.ws_col = 47;
        WS.ws_row = 35;
    }

    // register signals
    signal(SIGWINCH, on_resize);
    signal(SIGTERM, cleanup_err);
    signal(SIGINT, cleanup_err);

    term_init();

    struct Buffer buff = {0};
    if(buffer_init_from_path(&buff, "Makefile", FM_RW)) {
        perror("unable to open file");
        exit(1);
    }

    struct Buffer buff2 = {0};

    struct View views[2] = {
        {
            .buff = &buff,
            .line_off = 0,
            .view_cursor = {
                .off_x = 0,
                .off_y = 0,
            },
        },
        {
            .buff = &buff2,
            .line_off = 3,
            .view_cursor = {
                .off_x = 0,
                .off_y = 0,
            },
            .vp = &(struct ViewPort) {
                .height = 4,
                .width = 32,
                .off_x = 7,
                .off_y = 10,
            }
        },
    };

    struct View v2 = {
        .buff = &buff,
        .line_off = 0,
        .view_cursor = {
            .off_x = 0,
            .off_y = 0,
        },
    };

    // switch to alternate
    alternate_buf_enter();
    // register hook to return to normal buffer on exit
    atexit(alternate_buf_leave);

    struct Window win = {
        .child = NULL,
        .split_dir = SD_Vertical,
        .view_stack = views,
        .view_stack_len = 1,
    };

    struct Window win2 = win;
    struct Window win3 = win;
    win3.view_stack = views;
    win2.view_stack = &v2;
    win.child = &win2;
    win2.split_dir = SD_Horizontal;
    win2.child = &win3;

    win.view_stack_len = 2;

    struct Tab tab = {
        .name = NULL,
        .w = &win,
        .active_window = 0,
    };

    tabs_push(tab);
    tab.name = "theelo";
    tabs_push(tab);
    tabs_push(tab);
    tab.name = NULL;
    tabs_push(tab);
    tabs_push(tab);

    editor_render(&WS);
    while(RUNNING) {
        if(handle_keys()) editor_render(&WS);
        usleep(10);
    }

    buffer_cleanup(&buff);

    return 0;
}
