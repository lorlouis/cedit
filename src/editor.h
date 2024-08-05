#ifndef EDITOR_H
#define EDITOR_H 1

#include "termkey.h"
#include "maybe.h"
#include "str.h"
#include "buffer.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __MACH__
    #include <sys/ttycom.h>
#else
    #include <termio.h>
#endif

struct AbsoluteCursor;

extern struct winsize WS;

extern int RUNNING;

extern struct View MESSAGE;

enum Mode {
    M_Normal = 0,
    M_Insert = 1,
    M_Window = 2,
    M_Command = 3,
    M_Visual = 4,
    M_Search = 5,
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

// View Port is computed in absolute screen coordinates
typedef struct {
    uint16_t width;
    uint16_t height;

    uint16_t off_x;
    uint16_t off_y;
} ViewPort;

struct ViewOpt {
    int no_line_num;
};

struct AbsoluteCursor {
    uint16_t col;
    uint16_t row;
};

struct View {
    size_t line_off;
    size_t first_line_char_off;
    struct ViewCursor view_cursor;
    struct Buffer *buff;
    struct ViewOpt options;
    Style style;
    // whether this view manages it's own viewport
    _Bool viewport_locked;
    ViewPort vp;
    Maybe(ViewCursor) selection_end;
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
    Vec view_stack; // vec of Views
    size_t active_view;
};

struct Window window_new(void);

// Copies a window, but does not copy it's children
struct Window window_clone(struct Window *w);

void window_free_views(struct Window *w);
void window_free(struct Window *w);

struct View* window_view_active(struct Window *w);

void window_push(struct Window *win, struct Window *sub_win, enum SplitDir split);

int window_view_push(struct Window *w, struct View v);

int window_view_pop(struct Window *w, struct View *v);

struct Tab {
    struct Window w;
    size_t active_window;
    Str name;
};

struct Tab tab_new(struct Window w, const char *name);

void tab_free(struct Tab *t);

struct Window* tab_window_active(struct Tab *tab);

uint16_t viewport_viewable_width(const ViewPort *vp, const struct winsize *ws);
uint16_t viewport_viewable_height(const ViewPort *vp, const struct winsize *ws);

// does not clone `buff`
struct View view_new(struct Buffer *buff);

void view_clear(struct View *v);

struct View view_clone(struct View *v);

int view_render(
        struct View *v,
        ViewPort *vp,
        const struct winsize *ws,
        struct AbsoluteCursor *ac);

void view_move_cursor(struct View *v, ssize_t off_x, ssize_t off_y);

void view_set_cursor(struct View *v, size_t x, size_t y);

int view_write(struct View *v, const char *restrict s, size_t len);

void view_search_re(struct View *v);

struct ViewSelection {
    ViewCursor start;
    ViewCursor end;
};

int view_selection_position_selected(const struct ViewSelection *vs, size_t line_idx, size_t char_off);

int tabs_prev(void);

int tabs_next(void);

int tabs_push(struct Tab tab);

int tabs_pop(void);

struct Tab* tab_active(void);

int tabs_render(struct winsize *ws, struct AbsoluteCursor *ac);

struct Window* tab_window_active(struct Tab *tab);

struct View* tab_active_view(struct Tab *tab);

// editor api

void editor_init(void);

int editor_render(struct winsize *ws);

void editor_teardown(void);

int message_append(const char *fmt, ...);

int message_print(const char *fmt, ...);

// editor actions

void editor_quit_all(void);

void editor_quit(int no_confirm);

void editor_open(const char *path, enum FileMode fm, int no_confirm);

void editor_tabnew(const char *path, enum FileMode fm);

void editor_split_open(const char *path, enum FileMode fm, enum SplitDir split);

void editor_write(const char *path);

int clipboard_set(const char *s, size_t len);

int clipboard_get(Str *s);

void cursor_jump_prev_search(void);

void cursor_jump_next_search(void);

void editor_search(const char *re_str);


#endif
