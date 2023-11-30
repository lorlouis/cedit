#include "termkey.h"
#ifndef EDITOR_H
#define EDITOR_H 1

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __MACH__
    #include <sys/ttycom.h>
#else
    #include <termio.h>
#endif

struct AbsoluteCursor;

extern struct winsize WS;

enum FileMode {
    FM_RW,
    FM_RO,
};

struct Line {
    // does not include the null terminator
    size_t len;
    // includes the null terminator
    size_t cap;
    char *buf;
};

struct Input {
    enum {
        INPUTTY_SCRATCH,
        INPUTTY_FILE,
    } ty;
    union {
        int scratch:1;
        struct {
            char *path;
            enum FileMode fm;
        } file;
    } u;
};

enum Mode {
    M_Normal = 0,
    M_Insert = 1,
    M_Window = 2,
    M_Command = 3,
};

struct ModeInterface {
    const char* mode_str;
    int (*handle_key)(struct KeyEvent *e);
    int (*on_enter)(void);
    int (*on_leave)(void);
};

struct ModeInterface mode_current(void);

int mode_change(enum Mode mode);

typedef void (cleanup_fn)(void *data);

// Null initialise {0} to get a scratch buffer
struct Buffer {
    struct Input in;
    size_t lines_len;
    size_t lines_cap;
    struct Line *lines;
    size_t rc;
};

// increases the count and returns the data
// Returns
//  0 if the content does not exist
// >0 if the content exists
struct Buffer* buffer_rc_inc(struct Buffer *buff);

// decreases the count and frees if the count is 0
// Returns:
//  1 when the content of Rc got freed
//  0 when the content still exists
int buffer_rc_dec(struct Buffer *buff);

// View Port is computed in absolute screen coordinates
struct ViewPort {
    uint16_t width;
    uint16_t height;

    uint16_t off_x;
    uint16_t off_y;
};

struct ViewCursor {
    size_t off_x;
    size_t off_y;
};

struct ViewOpt {
    int no_line_num;
};

struct View {
    size_t line_off;
    struct ViewCursor view_cursor;
    struct Buffer *buff;
    struct ViewOpt options;
    struct ViewPort *vp;
};

enum Direction {
    DIR_Up = 1,
    DIR_Down,
    DIR_Left,
    DIR_Right
};

enum SplitDir {
    SD_Vertical,
    SD_Horizontal,
};

struct Window {
    enum SplitDir split_dir;
    struct Window *child;
    struct View *view_stack;
    size_t view_stack_len;
    size_t active_view;
};

struct Tab {
    struct Window *w;
    size_t active_window;
    char *name;
};

// Returns:
//  Non zero on success
//  0 on error and sets `errno`
FILE* filemode_open(
        enum FileMode fm,
        const char *path);

// Returns:
//  0 on success
//  -1 on error and sets `errno`
int filemode_save(
        enum FileMode fm,
        const char *path,
        struct Buffer *buff);

// Returns:
//  0 on success
//  -1 on error and sets `errno`
int buffer_init_from_path(
        struct Buffer *buff,
        char *path,
        enum FileMode fm);

// Frees the content of buffer
void buffer_cleanup(struct Buffer *buff);

uint16_t viewport_viewable_width(const struct ViewPort *vp, const struct winsize *ws);
uint16_t viewport_viewable_height(const struct ViewPort *vp, const struct winsize *ws);

int view_render(
        struct View *v,
        struct ViewPort *vp,
        const struct winsize *ws,
        struct AbsoluteCursor *ac);

void view_move_cursor(struct View *v, ssize_t off_x, ssize_t off_y);

int tabs_prev(void);

int tabs_next(void);

int tabs_push(struct Tab tab);

int tabs_render(struct winsize *ws, struct AbsoluteCursor *ac);

struct Window* tab_active_window(struct Tab *tab);

int editor_render(struct winsize *ws);

#endif
