#ifndef EDITOR_H
#define EDITOR_H 1

#include "termkey.h"

#include "str.h"

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

extern int RUNNING;

extern struct View MESSAGE;

enum FileMode {
    FM_RW,
    FM_RO,
};

struct Input {
    enum {
        INPUT_SCRATCH,
        INPUT_FILE,
    } ty;
    union {
        int scratch:1;
        struct {
            Str path;
            enum FileMode fm;
        } file;
    } u;
};

enum Mode {
    M_Normal = 0,
    M_Insert = 1,
    M_Window = 2,
    M_Command = 3,
    M_Visual = 4,
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

struct Line {
    Str text;
    size_t render_width;
};

// Null initialise {0} to get a scratch buffer
struct Buffer {
    struct Input in;
    enum FileMode fm;
    // vec of lines
    Vec lines;
    int dirty;
    size_t rc;
};

struct Line *buffer_line_get(struct Buffer *buff, size_t idx);

int buffer_line_insert(struct Buffer *buff, size_t idx, struct Line line);

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

enum MaybeVariant {
    None,
    Some,
};

#define Maybe(T) \
    struct Maybe##T { \
        enum MaybeVariant o; \
        union { \
            char none; \
            T some; \
        }u;\
    }

#define is_some(m) ((m).o == Some)
#define is_none(m) ((m).o == None)
#define as_ptr(m) ((m).o == Some ? &(m).u.some : 0)
#define Some(...) {.o = Some, .u.some = __VA_ARGS__ }
#define set_some(v, ...) {(v)->o = Some; (v)->u.some = __VA_ARGS__; }
#define set_none(v) {(v)->o = None;}
#define match_maybe(maybe, some_name, some_block, none_block) switch((maybe)->o) {\
        case Some: { \
            typeof((maybe)->u.some) *some_name = &(maybe)->u.some; \
            { some_block }\
        }; break; \
        case None: { \
            { none_block }\
        }; break; \
    }

enum ResultVariant {
    Ok,
    Err,
};

#define Result(OK_TY, ERR_TY) \
   struct Result##OK_TY##ERR_TY { \
       enum ResultVariant o; \
       union { \
           ERR_TY err; \
           OK_TY ok; \
        } u; \
    }

#define is_ok(m) ((m).o == Ok)
#define is_err(m) ((m).o == Err)
#define as_(m) ((m).o == Ok ? &(m).u.ok : 0)
#define Ok(...) {.o = Ok, .u.ok = __VA_ARGS__ }
#define Err(...) {.o = Err, .u.err = __VA_ARGS__ }
#define set_ok(v, ...) {(v)->o = Ok; (v)->u.ok = __VA_ARGS__; }
#define set_err(v, ...) {(v)->o = Err; (v)->u.err = __VA_ARGS__; }
#define match_result(res, ok_name, ok_block, err_name, err_block) switch((res)->o) {\
        case Ok: { \
            typeof((res)->u.ok) *ok_name = &(res)->u.ok; \
            { ok_block }\
        }; break; \
        case Err: { \
            typeof((res)->u.err) *err_name = &(res)->u.err; \
            { err_block }\
        }; break; \
    }

// View Port is computed in absolute screen coordinates
typedef struct {
    uint16_t width;
    uint16_t height;

    uint16_t off_x;
    uint16_t off_y;
} ViewPort;

typedef struct ViewCursor {
    size_t off_x;
    size_t off_y;
} ViewCursor;

struct ViewOpt {
    int no_line_num;
};


struct AbsoluteCursor {
    uint16_t col;
    uint16_t row;
};

struct RenderPlan {
    const ViewPort *vp;
    const struct winsize *ws;
    size_t fully_rendered_lines;
    size_t last_line_chars;
    struct AbsoluteCursor cursor;
    uint16_t real_height;
    uint16_t real_width;
    uint16_t line_max_width;
    int num_width;
    int prefix_width;
};


struct View {
    size_t line_off;
    struct ViewCursor view_cursor;
    struct Buffer *buff;
    struct ViewOpt options;
    Style style;
    // whether this view manages it's own viewport
    _Bool viewport_locked;
    ViewPort vp;
    Maybe(ViewCursor) selection_end;
    struct RenderPlan rp;
};

// does not clone `buff`
struct View view_new(struct Buffer *buff);

void view_clear(struct View *v);

struct View view_clone(struct View *v);

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
        const char *path,
        enum FileMode fm);

// Frees the content of buffer
void buffer_cleanup(struct Buffer *buff);

uint16_t viewport_viewable_width(const ViewPort *vp, const struct winsize *ws);
uint16_t viewport_viewable_height(const ViewPort *vp, const struct winsize *ws);

int view_render(
        struct View *v,
        ViewPort *vp,
        const struct winsize *ws,
        struct AbsoluteCursor *ac);

void view_move_cursor(struct View *v, ssize_t off_x, ssize_t off_y);

int view_write(struct View *v, const char *restrict s, size_t len);

int tabs_prev(void);

int tabs_next(void);

int tabs_push(struct Tab tab);

int tabs_pop(void);

int tabs_render(struct winsize *ws, struct AbsoluteCursor *ac);

struct Window* tab_active_window(struct Tab *tab);

// editor api

void editor_init(void);

int editor_render(struct winsize *ws);

void editor_teardown(void);

int message_append(const char *fmt, ...);

int message_print(const char *fmt, ...);

// editor actions

void editor_quit_all(void);

void editor_quit(void);

void editor_open(const char *path, enum FileMode fm);

void editor_tabnew(const char *path, enum FileMode fm);

void editor_split_open(const char *path, enum FileMode fm, enum SplitDir split);

void editor_write(const char *path);

int clipboard_set(char *s, size_t len);

#endif
