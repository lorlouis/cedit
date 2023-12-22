#include "editor.h"
#include "vt.h"
#include "xalloc.h"
#include "str.h"
#include "commands.h"
#include "config.h"

#include <wordexp.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <wctype.h>
#include <math.h>

struct winsize WS = {0};

int RUNNING = 1;

struct AbsoluteCursor {
    uint16_t col;
    uint16_t row;
};

struct View MESSAGE = {
    .buff = 0,
    .options = {
        .no_line_num = 0,
    }
};

// len in character idx
size_t render_width(Str *s, size_t len) {
    size_t width = 0;
    for(size_t i = 0; i < len; i++) {
        utf32 c = 0;
        assert(!str_get_char(s, i, &c));
        width += utf32_width(c);
    }
    return width;
}

struct Line line_from_cstr(char *s) {
    Str str = str_from_cstr(s);
    return (struct Line) {
        .render_width = render_width(&str, str_len(&str)),
        .text = str,
    };
}

struct Line line_new(void) {
    return (struct Line){0};
}

void line_free(struct Line *l) {
    str_free(&l->text);
}

int line_insert_at(struct Line *l, size_t idx, const char *s, size_t len) {
    int ret = str_insert_at(&l->text, idx, s, len);
    l->render_width = render_width(&l->text, str_len(&l->text));
    return ret;
}

void line_trunk(struct Line *l, size_t idx) {
    Str tail = str_tail(&l->text, idx);
    size_t trunked_width = render_width(&tail, str_len(&tail));
    str_trunc(&l->text, idx);
    l->render_width -= trunked_width;
}

void line_clear(struct Line *l) {
    line_trunk(l, 0);
}

int line_append(struct Line *l, const char *s, size_t len) {
    int ret = str_push(&l->text, s, len);
    l->render_width = render_width(&l->text, str_len(&l->text));
    return ret;
}

void message_clear(void) {
    view_clear(&MESSAGE);
}

int message_append(const char *fmt, ...) {
    va_list args;
    int ret;
    va_start(args, fmt);
    size_t s_size = 0;
    char *formatted = 0;
    ret = vasprintf(&formatted, fmt, args);
    va_end(args);
    if(ret < 0) {
        return -1;
    }
    s_size = (size_t)ret;

    view_write(&MESSAGE, formatted, s_size);
    free(formatted);

    return s_size;
}

int message_print(const char *fmt, ...) {
    va_list args;
    int ret;
    va_start(args, fmt);
    size_t s_size = 0;
    char *formatted = 0;
    ret = vasprintf(&formatted, fmt, args);
    va_end(args);
    if(ret < 0) {
        return -1;
    }
    s_size = (size_t)ret;

    view_clear(&MESSAGE);
    view_write(&MESSAGE, formatted, s_size);
    free(formatted);

    return s_size;
}

FILE* filemode_open(
        enum FileMode fm,
        const char *path) {
    char *mode;

    switch(fm) {
        case FM_RW:
            mode = "r+";
            break;
        case FM_RO:
            mode = "r";
            break;
    }

    return fopen(path, mode);
}

// Returns -1 on error and sets errno
int buffer_dump(
        struct Buffer *buff,
        char *path) {

    if(buff->fm == FM_RO) {
        errno = EINVAL;
        return -1;
    }

    FILE *f = fopen(path, "w");
    if(!f) {
        return -1;
    }

    for(size_t i = 0; i < buff->lines.len; i++) {
        struct Line *line = buffer_line_get(buff, i);
        int ret = fprintf(f, "%s\n", str_as_cstr(&line->text));
        if(ret < 0) {
            errno = ferror(f);
            fclose(f);
            return -1;
        }
    }

    fclose(f);
    return 0;
}


// Returns -1 on error and sets errno
int buffer_init_from_path(
        struct Buffer *buff,
        const char *path,
        enum FileMode fm) {

    errno = 0;
    FILE *f = filemode_open(fm, path);
    if(!f) return -1;

    Vec lines = VEC_NEW(struct Line, (void(*)(void*))line_free);

    char *line=0;
    size_t cap=0;
    ssize_t len=0;
    errno = 0;
    while((len=getline(&line, &cap, f)) > 0) {

        if(line[len-1] == '\n') {
            line[len-1] = '\0';
            len-=1;
        }

        Str s = str_from_cstr_len(line, len+1);
        vec_push(&lines, &s);
    }
    free(line);

    if(len == -1 && ferror(f)) {
        errno = ferror(f);
        goto err;
    }
    fclose(f);

    buff->in.ty = INPUT_FILE;
    buff->in.u.file.fm = fm;
    buff->in.u.file.path = str_from_cstr(path);

    buff->lines = lines;
    buff->rc = 0;
    buff->fm = fm;

    return 0;

    err:
        vec_cleanup(&lines);
        fclose(f);
        return -1;
}


struct Line *buffer_line_get(struct Buffer *buff, size_t idx) {
    buff->lines.type_size = sizeof(struct Line);
    return VEC_GET(struct Line, &buff->lines, idx);
}

void buffer_line_remove(struct Buffer *buff, size_t idx) {
    vec_remove(&buff->lines, idx);
}

int buffer_line_insert(struct Buffer *buff, size_t idx, struct Line line) {
    buff->lines.type_size = sizeof(struct Line);
    return vec_insert(&buff->lines, idx, &line);
}

// DO NOT USE DIRECTLY, USE `buffer_rc_dec`
void buffer_cleanup(struct Buffer *buff) {
    vec_cleanup(&buff->lines);
    switch(buff->in.ty) {
        case INPUT_SCRATCH:
            break;
        case INPUT_FILE:
            str_free(&buff->in.u.file.path);
            break;
    }
    memset(buff, 0, sizeof(struct Buffer));
}

// if fd is -1, nothing will be printed
// Returns the width (in columns) of the character
// -1 on error
int write_char_escaped(Style *base_style, utf32 c, int fd) {
    char char_buffer[4] = {0};
    int count = 0;

    Style substitution = style_new();
    if(base_style) {
        substitution = style_fg(*base_style, colour_vt(VT_GRA));
    }

    wint_t wc = utf32_to_wint(c);
    if(wc == L'\n' || wc == 0) {
        return 0;
    }
    else if(wc == (wint_t)-1) {
        return -1;
    }

    if(wc == L'\t') {
        Style style = {0};
        style = style_fg(style, colour_vt(VT_GRA));

        int tail_len = CONFIG.tab_width -1;
        if(CONFIG.tab_width == 0) {
            tail_len = 0;
        }
        if(fd != -1) {
            style_fmt(&substitution, fd, "%*c%*c", CONFIG.tab_width > 0, '>', tail_len, '-');
        }
        count += CONFIG.tab_width;
    } else if (iswprint(wc)) {
        utf32_to_utf8(c, char_buffer, 4);
        if(fd != -1) {
            write(fd, char_buffer, utf32_len_utf8(c));
        }
        count += utf32_width(c);
    }

    return count;
}

int write_escaped(Style *base_style, const Str *line, size_t len) {

    size_t off = 0;
    int count = 0;
    int ret;

    while(off < len) {
        utf32 c = 0;
        assert(str_get_char(line, off, &c) != -1);

        // could be wide null (unlikely but still)
        assert(c && "null in middle of the line");
        assert(c != L'\n' && "new line in middle of the line");

        ret = write_char_escaped(base_style, c, STDOUT_FILENO);
        if(ret == -1) return ret;
        count += ret;
        off += 1;
    }
    return count;
}

uint16_t viewport_viewable_width(const ViewPort *vp, const struct winsize *ws) {
    uint16_t real_width = vp->width;
    if(vp->width + vp->off_x > ws->ws_col) {
        real_width = vp->width - ((vp->width + vp->off_x) - ws->ws_col);
    }
    return real_width;
}

uint16_t viewport_viewable_height(const ViewPort *vp, const struct winsize *ws) {
    uint16_t real_height = vp->height;
    if(vp->height + vp->off_y >= ws->ws_row) {
        real_height = vp->height - ((vp->height + vp->off_y) - ws->ws_row);
    }
    return real_height;
}

uint16_t viewport_width_clamp(const ViewPort *vp, const struct winsize *ws, uint16_t v) {
    if(v < vp->off_x) return vp->off_x;
    if(v > vp->off_x + ws->ws_col -1) return vp->off_x + ws->ws_col -1;
    return v;
}

uint16_t viewport_height_clamp(const ViewPort *vp, const struct winsize *ws, uint16_t v) {
    if(v < vp->off_y) return vp->off_y;
    if(v > vp->off_y + ws->ws_row-1) return vp->off_y + ws->ws_row-1;
    return v;
}

void view_clear(struct View *v) {
    v->line_off = 0;
    v->view_cursor.off_x = 0;
    v->view_cursor.off_y = 0;
    for(size_t i = 0; i < v->buff->lines.len; i++) {
        line_clear(buffer_line_get(v->buff, i));
    }
}

void view_free(struct View *v) {
    buffer_rc_dec(v->buff);
}

int view_write(struct View *v, const char *restrict s, size_t len) {
    if(len == 0) return 0;
    v->buff->dirty = 1;

    size_t off = 0;

    size_t line_idx = v->view_cursor.off_y;

    // make sure there is a line under the cursor
    // needed when the buffer is empty
    if(line_idx == v->buff->lines.len) {
        buffer_line_insert(v->buff, line_idx, line_new());
    }

    struct Line *line = buffer_line_get(v->buff, line_idx);
    char *end_of_line = 0;
    size_t end_of_line_size = 0;
    size_t source_line = v->view_cursor.off_y;

    // save end of line and null terminator
    end_of_line_size = str_cstr_len(&line->text) - str_get_char_byte_idx(&line->text, v->view_cursor.off_x);
    end_of_line = xcalloc(end_of_line_size, sizeof(char));

    memcpy(end_of_line, str_as_cstr(&line->text) + str_get_char_byte_idx(&line->text, v->view_cursor.off_x), end_of_line_size);

    while(off < len) {
        size_t start = off;
        while(s[off] != '\n' && s[off] != '\0') off++;
        size_t size = off - start;
        size_t cursor = str_get_char_byte_idx(&line->text, v->view_cursor.off_x);

        size_t original_len = str_len(&line->text);

        // copy the new block on top of the old stuff
        line_insert_at(line, v->view_cursor.off_x, s + start, size);

        size_t new_len = str_len(&line->text);

        v->view_cursor.off_x += new_len - original_len;

        if(s[off] == '\n') {
            if(cursor <= str_len(&line->text)) {
                line_trunk(line, cursor);
            }
            off++;
            // insert line under cursor
            buffer_line_insert(v->buff, v->view_cursor.off_y+1, line_new());
            // select the new line
            v->view_cursor.off_y += 1;
            line = buffer_line_get(v->buff, v->view_cursor.off_y);

            // move cursor to the end of that line
            v->view_cursor.off_x = str_len(&line->text);
        }
    }
    if(end_of_line_size) {
        // write end of line at the end of the current cursor line
        // if the line changed
        if(source_line != v->view_cursor.off_y) {
            line_append(line, end_of_line, end_of_line_size);
        }
    }
    free(end_of_line);
    return 0;
}

void view_set_cursor(struct View *v, size_t x, size_t y) {
    if(!v->buff || !v->buff->lines.len) return;
    v->view_cursor.off_y = y < v->buff->lines.len ? y : v->buff->lines.len-1;
    struct Line *l = buffer_line_get(v->buff, v->view_cursor.off_y);

    v->view_cursor.off_x = x < str_len(&l->text) ? x : str_len(&l->text);
}

struct ViewCursor view_get_cursor(struct View *v) {
    return v->view_cursor;
}

void view_move_cursor_start(struct View *v) {
    view_set_cursor(v, 0, v->view_cursor.off_y);
}

void view_move_cursor_end(struct View *v) {
    view_set_cursor(v, SIZE_MAX, v->view_cursor.off_y);
}

void view_move_cursor(struct View *v, ssize_t off_x, ssize_t off_y) {
    ssize_t new_x = (ssize_t)v->view_cursor.off_x + off_x;
    if(new_x < 0) new_x = 0;
    ssize_t new_y = (ssize_t)v->view_cursor.off_y + off_y;
    if(new_y < 0) new_y = 0;

    view_set_cursor(v, new_x, new_y);
}

// TODO(louis) rewrite this. I hate it
int view_render(struct View *v, ViewPort *vp, const struct winsize *ws, struct AbsoluteCursor *ac) {
    ViewPort *real_vp = vp;

    Style view_bg = v->style;

    Style num_text = style_fg(view_bg, colour_vt(VT_BLU));

    if(v->viewport_locked) {
        real_vp = &v->vp;
    }

    uint16_t real_width = viewport_viewable_width(real_vp, ws);
    uint16_t real_height = viewport_viewable_height(real_vp, ws);

    if (v->line_off > v->view_cursor.off_y) {
        v->line_off = v->view_cursor.off_y;
    } else if(v->view_cursor.off_y - v->line_off > real_height) {
        v->line_off += (v->view_cursor.off_y - v->line_off) - real_height;
    }

    int num_width = 0;
    if(!v->options.no_line_num) {
        // compute the number of chars to write the line number + 1
        num_width = ceil(log10((double)v->buff->lines.len)) + 1;
        if(num_width < 2) num_width = 2;
    }

    uint16_t text_width = real_width - num_width;

    size_t *extra_render_line_per_line = xcalloc(real_height, sizeof(size_t));
    size_t line_off = 0;
    uint16_t cursor_line_off = 0;
    size_t i = 0;
    for(;
            i + v->line_off < v->buff->lines.len && i + line_off < real_height;
            i++) {
        struct Line *l = buffer_line_get(v->buff, i+line_off+v->line_off);

        size_t char_off = 0;
        size_t extra_render_line_count = 0;
        while(i + line_off + extra_render_line_count < real_height) {
            ssize_t len = 0;
            size_t take_width = text_width;

            if(char_off < str_len(&l->text)) {
                Str tail = str_tail(&l->text, char_off);
                len = take_cols(&tail, &take_width, CONFIG.tab_width);
            } else {
                take_width = 0;
            }

            assert(len >= 0 && "bad string");

            set_cursor_pos(real_vp->off_x, real_vp->off_y+i+line_off + extra_render_line_count);

            if (num_width) {
                style_fmt(
                        &num_text,
                        STDOUT_FILENO,
                        "%*ld ",
                        num_width - 1,
                        i + v->line_off + 1
                    );
            }

            Str tail = str_tail(&l->text, char_off);

            write_escaped(&view_bg, &tail, len);

            // fill the rest of the line, useful when views overlap
            int fill = text_width-take_width;
            if(fill) {
                style_fmt(&view_bg, STDOUT_FILENO, "%*s", fill, " ");
            }

            // reached the end of the line
            if((size_t)len + char_off >= str_len(&l->text)) break;

            char_off += len;
            extra_render_line_count += 1;
        }
        extra_render_line_per_line[i] = extra_render_line_count;
        line_off += extra_render_line_count;

        // adjust y offset for cursor
        if(v->view_cursor.off_y > i+v->line_off) {
            cursor_line_off = line_off;
        }

    }

    for(int j = i + line_off; j < real_height; j++) {
        set_cursor_pos(real_vp->off_x, real_vp->off_y+j);
        if (num_width) {
            style_fmt(
                    &num_text,
                    STDOUT_FILENO,
                    "%*c ",
                    num_width - 1,
                    '~'
                );
        }
        style_fmt(
                &view_bg,
                STDOUT_FILENO,
                "%*s",
                real_width-num_width,
                " ");
    }

    if(ac && v->buff->lines.len) {
        struct Line *line = buffer_line_get(v->buff, v->view_cursor.off_y);
        Str line_until_col = str_head(&line->text, v->view_cursor.off_x + 1);
        uint16_t col = count_cols(&line_until_col, 4);
        assert(col != (uint16_t)-1);
        uint16_t row = v->view_cursor.off_y - v->line_off + cursor_line_off;

        if(col >= real_vp->width - num_width) {
            int16_t diff = col - (real_vp->width - num_width);
            size_t len = 0;
            while(diff >= 0) {
                size_t take_width = real_vp->width - num_width;
                Str tail = str_tail(&line->text, len);
                Str tail_head = str_head(&tail, v->view_cursor.off_x - len + 1);
                size_t new_len = take_cols(&tail_head, &take_width, 4);
                row += 1;
                col -= take_width;
                diff -= take_width;
                len = new_len;
            }
        }

        if(row >= real_vp->height) {
            uint16_t diff = row - real_vp->height;
            uint16_t line_off = 0;
            uint16_t render_lines = 0;
            while(render_lines <= diff) {
                render_lines += extra_render_line_per_line[line_off]+1;
                line_off += 1;
            }
            v->line_off += line_off;
            // FIXME(louis) this is so ugly, the layout should be computed
            // first and then rendered
            free(extra_render_line_per_line);
            return view_render(v, vp, ws, ac);
        }
        else {
            // TODO check col here, it looks like this is where the é bug happens
            ac->col = viewport_width_clamp(real_vp, ws, col + num_width + real_vp->off_x);
            ac->row = viewport_height_clamp(real_vp, ws, row + real_vp->off_y);
        }
    } else if (ac && ! v->buff->lines.len) {
        ac->col = viewport_width_clamp(real_vp, ws, num_width + real_vp->off_x);
        ac->row = viewport_height_clamp(real_vp, ws, real_vp->off_y);
    }
    free(extra_render_line_per_line);

    return 0;
}

struct Buffer* buffer_rc_inc(struct Buffer *buff) {
    buff->rc += 1;
    return buff;
}

int buffer_rc_dec(struct Buffer *buff) {
    if(!buff->rc) {
        buffer_cleanup(buff);
        xfree(buff);
        return 1;
    }
    buff->rc -= 1;
    return 0;
}

struct Tab tab_new(struct Window w, const char *name) {
    return (struct Tab) {
        .w = w,
        .active_window = 0,
        .name = str_from_cstr(name),
    };
}

void tab_free(struct Tab *t) {
    str_free(&t->name);
    window_free(&t->w);
}

int tab_remove_window(struct Tab *t, int id) {
    size_t idx = t->active_window;
    if(id > 0) {
        idx = (size_t)id;
    }

    struct Window *parent = 0;
    struct Window *current = &t->w;
    for(size_t i = 0; i < idx; i++) {
        parent = current;
        current = current->child;
        assert(current && "index out of range");
    }
    // not the first node
    if(parent) {
        parent->child = current->child;
        window_free_views(current);
        free(current);
        if(t->active_window >= idx) {
            t->active_window -= 1;
        }
    } else {
        struct Window *child = current->child;
        window_free_views(current);
        if(child) {
            t->w = *child;
            xfree(child);
        }
        return -1;
    }
    return 0;
}

Vec TABS = VEC_NEW(struct Tab, (void(*)(void*))tab_free);
size_t ACTIVE_TAB = 0;


struct Tab* tab_get(size_t idx) {
    return VEC_GET(struct Tab, &TABS, idx);
}

struct Tab* tab_active(void) {
    return tab_get(ACTIVE_TAB);
}

int tabs_push(struct Tab tab) {
    vec_push(&TABS, &tab);
    return TABS.len;
}

int tabs_remove(int id) {
    size_t idx = ACTIVE_TAB;
    if(id > 0) {
        idx = (size_t)id;
    }

    vec_remove(&TABS, idx);

    if(ACTIVE_TAB >= idx && ACTIVE_TAB > 0) {
        ACTIVE_TAB -= 1;
    }

    return TABS.len;
}

int tabs_pop(void) {
    vec_pop(&TABS, 0);
    if(ACTIVE_TAB >= TABS.len) {
        ACTIVE_TAB = TABS.len -1;
    }

    return TABS.len;
}

int window_view_push(struct Window *w, struct View v) {
    w->view_stack.type_size = sizeof(struct View);
    vec_push(&w->view_stack, &v);
    return w->view_stack.len;
}

int window_view_pop(struct Window *w, struct View *v) {
    w->view_stack.type_size = sizeof(struct View);
    vec_pop(&w->view_stack, v);
    return w->view_stack.len;
}

struct View* window_view_get(struct Window *w, size_t idx) {
    w->view_stack.type_size = sizeof(struct View);
    return VEC_GET(struct View, &w->view_stack, idx);
}

void window_close_view(struct Window *w, int id) {
    size_t idx = w->active_view;
    if(id >= 0) {
        idx = (size_t) id;
    }

    vec_remove(&w->view_stack, idx);

    if(w->active_view >= idx && w->active_view > 0) {
        w->active_view -= 1;
    }

    return;
}

struct View* window_view_active(struct Window *w) {
    return window_view_get(w, w->active_view);
}

struct Window window_new(void) {
    Vec view_stack = VEC_NEW(struct View, (void(*)(void*))view_free);
    return (struct Window) {
        .split_dir = SD_Vertical,
        .active_view = 0,
        .child = 0,
        .view_stack = view_stack,
    };
}

// Copies a window, but does not copy it's children
struct Window window_clone_shallow(struct Window *w) {
    Vec new_view_stack = VEC_NEW(struct View, (void(*)(void*))view_free);
    for(size_t i = 0; i < w->view_stack.len; i++) {
        struct View clone = view_clone(window_view_get(w, i));
        vec_push(&new_view_stack, (void*)&clone);
    }
    return (struct Window) {
        .split_dir = w->split_dir,
        .child = 0,
        .view_stack = new_view_stack,
        .active_view = w->active_view,
    };
}

void window_free_views(struct Window *w) {
    vec_cleanup(&w->view_stack);
}

void window_free(struct Window *w) {
    window_free_views(w);
    if(w->child) {
        window_free(w->child);
        w->child = 0;
    }
    w->child = 0;
    w->active_view = 0;
    w->split_dir = 0;
}

int window_render(struct Window *w, ViewPort *vp, const struct winsize *ws, struct AbsoluteCursor *ac) {
    Style line_style = {0};
    line_style = style_fg(line_style, colour_vt(VT_YEL));

    int is_odd = 0;
    ViewPort self_vp = *vp;

    if(w->child) {
        ViewPort sub_vp;
        switch(w->split_dir) {
            case SD_Vertical: {
                // Look mah! No npm
                is_odd = (self_vp.width-1) % 2;
                self_vp.width = (self_vp.width-1) / 2;
                sub_vp = self_vp;
                self_vp.width += is_odd;
                sub_vp.off_x += self_vp.width+1;

                // render split line
                for(int i = 0; i < self_vp.height; i++) {
                    set_cursor_pos(self_vp.width + vp->off_x, i + vp->off_y);
                    style_fmt(&line_style,  STDERR_FILENO, "|");
                }
            } break;
            case SD_Horizontal: {
                is_odd = (self_vp.height-1) % 2;
                self_vp.height = (self_vp.height-1) / 2;
                sub_vp = self_vp;
                self_vp.height += is_odd;
                sub_vp.off_y += self_vp.height+1;

                // render split line
                for(int i = 0; i <= self_vp.width; i++) {
                    set_cursor_pos(i + vp->off_x, vp->off_y+self_vp.height);
                    style_fmt(&line_style,  STDERR_FILENO, "-");
                }

            } break;
        }
        // render subwindow(s)
        window_render(w->child, &sub_vp, ws, ac);
        // render self window with cursor
        for(size_t i = 0; i < w->view_stack.len; i++) {
            struct View *v = window_view_get(w, i);
            if(!v->viewport_locked) {
                v->vp = self_vp;
            }
            if(i == w->active_view && tab_window_active(tab_active()) == w ) {
                view_render(v, &self_vp, ws, ac);
            } else {
                view_render(v, &self_vp, ws, 0);
            }
        }
        self_vp = sub_vp;
    } else {
        if(tab_window_active(tab_active()) == w) {
            // render self window with cursor
            for(size_t i = 0; i < w->view_stack.len; i++) {
                if(i == w->active_view) {
                    view_render(window_view_get(w, i), &self_vp, ws, ac);
                } else {
                    view_render(window_view_get(w, i), &self_vp, ws, 0);
                }
            }
        } else {
            // render self window
            for(size_t i = 0; i < w->view_stack.len; i++) {
                view_render(window_view_get(w, i), &self_vp, ws, 0);
            }
        }
    }
    return 0;
}

struct Window* tab_get_window(struct Tab *tab, size_t idx) {
    struct Window *w = &tab->w;
    // select current window
    for(size_t i = 0; i < idx; i++) {
        w = w->child;
    }
    return w;
}

struct Window* tab_window_active(struct Tab *tab) {
    return tab_get_window(tab, tab->active_window);
}

struct View* tab_active_view(struct Tab *tab) {
    struct Window *w = tab_window_active(tab);
    return window_view_active(w);
}

int tabs_prev(void) {
    if(ACTIVE_TAB > 0) {
        ACTIVE_TAB -= 1;
    }
    return ACTIVE_TAB;
}

int tabs_next(void) {
    if(ACTIVE_TAB < TABS.len-1) ACTIVE_TAB += 1;
    return ACTIVE_TAB;
}

int tabs_win_select(enum Direction dir) {
    struct Tab *cur_tab = tab_active();
    struct Window *cur_window = tab_window_active(cur_tab);
    switch(dir) {
        // looks for next window
        case DIR_Down: {
            if(cur_window->split_dir == SD_Horizontal && cur_window->child) {
                cur_tab->active_window += 1;
            }
        } break;
        case DIR_Right: {
            if(cur_window->split_dir == SD_Vertical && cur_window->child) {
                cur_tab->active_window += 1;
            }
        } break;
        // looks for prev window
        case DIR_Up: {
            int offset = cur_tab->active_window;
            offset -= 1;

            while(offset >= 0) {
                cur_window = tab_get_window(cur_tab, offset);
                if(cur_window->split_dir == SD_Horizontal) {
                    cur_tab->active_window = offset;
                    break;
                }
                offset-=1;
            }
        } break;
        case DIR_Left: {
            int offset = cur_tab->active_window;
            offset -= 1;

            while(offset >= 0) {
                cur_window = tab_get_window(cur_tab, offset);
                if(cur_window->split_dir == SD_Vertical) {
                    cur_tab->active_window = offset;
                    break;
                }
                offset-=1;
            }

        } break;
    }

    return 0;
}

int tabs_render(struct winsize *ws, struct AbsoluteCursor *ac) {
    Style unselected = style_bg(style_new(), colour_vt(VT_GRA));
    Style selected = style_bg(style_new(), colour_none());

    selected = style_fg(selected, colour_vt(VT_BLU));

    size_t sum = 0;
    // render top bar
    set_cursor_pos(0,0);
    style_fmt(&unselected, STDOUT_FILENO, "%*c", ws->ws_col, ' ');
    set_cursor_pos(0,0);
    style_begin(&unselected, STDOUT_FILENO);
    for(size_t i = 0; i < TABS.len; i++) {
        if(sum + 6 >= ws->ws_col) break;

        if(i == ACTIVE_TAB) {
            style_reset(STDOUT_FILENO);
            style_begin(&selected, STDOUT_FILENO);

            if(!str_is_empty(&tab_get(i)->name)) {
                size_t cols = 10;
                int len = take_cols(&tab_get(i)->name, &cols, CONFIG.tab_width);
                assert(len >= 0 && "error when computing len");
                dprintf(STDOUT_FILENO, "[%.*s]", len, str_as_cstr(&tab_get(i)->name));
                sum+=cols+2;
            } else {
                dprintf(STDOUT_FILENO, "[%.4ld]", i);
                sum+=6;
            }

            style_reset(STDOUT_FILENO);
            style_begin(&unselected, STDOUT_FILENO);
        } else {
            if(!str_is_empty(&tab_get(i)->name)) {
                size_t cols = 10;
                int len = take_cols(&tab_get(i)->name, &cols, CONFIG.tab_width);
                assert(len >= 0 && "error when computing len");
                dprintf(STDOUT_FILENO, " %.*s ", len, str_as_cstr(&tab_get(i)->name));
                sum+=cols+2;
            } else {
                dprintf(STDOUT_FILENO, " %.4ld ", i);
                sum+=6;
            }
        }
    }
    style_reset(STDOUT_FILENO);
    ViewPort vp = {
        // some space to put the status line
        .height = ws->ws_row - 3,
        .width = ws->ws_col,
        // screen coordinate start at 1
        .off_x = 0,
        // skip the tabs
        .off_y = 1,
    };

    window_render(&tab_active()->w, &vp, ws, ac);

    return 0;
}

// does not clone `buff`
struct View view_new(struct Buffer *buff) {
    return (struct View) {
        .line_off = 0,
        .view_cursor = {0},
        .buff = buff,
        .options = {0},
        .vp = {0},
    };
}

struct View view_clone(struct View *v) {
    struct View view = *v;
    buffer_rc_inc(view.buff);
    return view;
}

int view_erase(struct View *v) {
    size_t cursor = v->view_cursor.off_x;

    struct Line *line = buffer_line_get(v->buff, v->view_cursor.off_y);
    if(cursor > 0) {
        size_t start = cursor-1;
        if(CONFIG.use_spaces && cursor >= 4 && !(cursor%4)) {
            _Bool is_a_tab = true;
            utf32 c = 0;
            for(int i = 1; i <= CONFIG.tab_width; i++) {
                str_get_char(&line->text, cursor - i, &c);
                if(c != L' ') {
                    is_a_tab = false;
                    break;
                }
            }
            if(is_a_tab) {
                start = cursor - CONFIG.tab_width;
            }
        }

        str_remove(&line->text, start, cursor-1);
        v->view_cursor.off_x -= cursor - start;
    } else if(v->view_cursor.off_y > 0) {
        struct Line *prev_line = buffer_line_get(v->buff, v->view_cursor.off_y -1);
        // move cursor to end of previous line
        v->view_cursor.off_x = str_len(&prev_line->text);
        str_push(&prev_line->text, str_as_cstr(&line->text), str_cstr_len(&line->text));

        buffer_line_remove(v->buff, v->view_cursor.off_y);
        v->view_cursor.off_y -= 1;
    }
    return 0;
}

void view_next(void) {
    struct Tab *cur_tab = tab_active();
    struct Window *cw = tab_window_active(cur_tab);
    if(cw->view_stack.len > 0 && cw->active_view < cw->view_stack.len-1) {
        cw->active_view += 1;
    } else {
        cw->active_view = 0;
    }
}

void view_prev(void) {
    struct Tab *cur_tab = tab_active();
    struct Window *cw = tab_window_active(cur_tab);
    if(cw->active_view > 0) {
        cw->active_view -= 1;
    } else if(cw->view_stack.len > 0){
        cw->active_view = cw->view_stack.len-1;
    }
}


// DO NOT USE DIRECTLY, use mode_* functions
static enum Mode MODE = M_Normal;

int normal_handle_key(struct KeyEvent *e) {
    struct View *v = tab_active_view(tab_active());

    if(e->modifier == 0) {
        switch(e->key) {
            case 'G': {
                view_move_cursor(v, 0, SIZE_MAX);
            } break;
            case 'O': {
                view_move_cursor_start(v);
                view_write(v, "\n", sizeof("\n")-1);
                view_move_cursor(v, 0, -1);
            } break;
            case 'o': {
                view_move_cursor_end(v);
                view_write(v, "\n", sizeof("\n")-1);
            } break;
            case 'v': {
                mode_change(M_Visual);
            } break;
            case '$': {
                view_move_cursor_end(v);
            } break;
            case '0': {
                view_move_cursor_start(v);
            } break;
            case KC_ARRDOWN:
            case 'j': {
                view_move_cursor(v, 0,+1);
            } break;
            case KC_ARRUP:
            case 'k': {
                view_move_cursor(v, 0,-1);
            } break;
            case KC_ARRLEFT:
            case 'h': {
                view_move_cursor(v, -1,0);
            } break;
            case KC_ARRRIGHT:
            case 'l': {
                view_move_cursor(v, +1,0);
            } break;
            case 'i': {
                mode_change(M_Insert);
            } break;
            case ':': {
                mode_change(M_Command);
            } break;
            default:
                break;
        }
    } else if (e->modifier == KM_Ctrl) {
        switch(e->key) {
            case 'e': {
                if(v->line_off < v->buff->lines.len-1) {
                    if(v->view_cursor.off_y == v->line_off) {
                        v->view_cursor.off_y += 1;
                    }
                    v->line_off += 1;
                }
            } break;
            case KC_ARRLEFT:
            case 'h': {
                tabs_prev();
            } break;
            case KC_ARRRIGHT:
            case 'l': {
                tabs_next();
            } break;
            case 'w': {
                mode_change(M_Window);
            } break;
            default:
                break;
        }
    }

    return 0;
}

int insert_enter(void) {
    char line_cursor[] = CSI"5 q";
    write(STDOUT_FILENO, line_cursor, sizeof(line_cursor)-1);
    return 0;
}

int insert_leave(void) {
    char block_cursor[] = CSI"1 q";
    write(STDOUT_FILENO, block_cursor, sizeof(block_cursor)-1);
    return 0;
}

int insert_handle_key(struct KeyEvent *e) {
    struct View *v = tab_active_view(tab_active());
    if(e->key == '\e') {
        mode_change(M_Normal);
        return 0;
    } else if(e->key == '\t' && CONFIG.use_spaces) {
        for(int i = 0; i < CONFIG.tab_width; i++) {
            view_write(v, " ", 1);
        }
    } else if(e->key == KC_BACKSPACE) {
        view_erase(v);
        return 0;
    } else {
        char buff[4] = {0};
        int len = utf32_to_utf8(e->key, buff, sizeof(buff));
        view_write(v, buff, len);
    }

    return 0;
}

int window_handle_key(struct KeyEvent *e) {
    switch(e->key) {
        case KC_ARRDOWN:
        case 'j': {
            tabs_win_select(DIR_Down);
            mode_change(M_Normal);
        } break;
        case KC_ARRUP:
        case 'k': {
            tabs_win_select(DIR_Up);
            mode_change(M_Normal);
        } break;
        case KC_ARRLEFT:
        case 'h': {
            tabs_win_select(DIR_Left);
            mode_change(M_Normal);
        } break;
        case KC_ARRRIGHT:
        case 'l': {
            tabs_win_select(DIR_Right);
            mode_change(M_Normal);
        } break;
        case 'i': {
            mode_change(M_Insert);
        } break;
        case '\e': {
            mode_change(M_Normal);
        } break;
        case 'w': {
            view_next();
            mode_change(M_Normal);
        } break;
        case 's': {
            view_prev();
            mode_change(M_Normal);
        } break;
        default:
            break;
    }
    return 0;
}

int command_enter(void) {
    message_print(":");
    insert_enter();
    return 0;
}

int command_leave(void) {
    insert_leave();
    return 0;
}

int command_handle_key(struct KeyEvent *e) {
    // TODO(louis) handle command buffer here
    if(e->modifier == 0) {
        if(e->key == '\e') {
            mode_change(M_Normal);
            return 0;
        } else if(e->key == KC_BACKSPACE) {
            // do not erase the leading ':'
            if(MESSAGE.view_cursor.off_x > 1) {
                view_erase(&MESSAGE);
            }
            return 0;
        } else if(e->key == '\n') {
            if(MESSAGE.buff->lines.len) {
                // TODO this only copies the first line
                struct Line *line = buffer_line_get(MESSAGE.buff, 0);
                Str command = str_clone(&line->text);
                message_clear();
                // exec command will write into the message buffer
                exec_command(str_as_cstr(&command));
                str_free(&command);
                mode_change(M_Normal);
            }
            return 0;
        }
    }
    switch(e->key) {
        default: {
            char bytes[4] = {0};
            int len = utf32_to_utf8(e->key, bytes, 4);
            assert(len >= 1);
            message_append("%.*s", len, bytes);
        } break;
    }
    return 0;
}

int visual_enter(void) {
    struct View *v = tab_active_view(tab_active());
    set_some(&v->selection_end, view_get_cursor(v));
    return 0;
}

int visual_leave(void) {
    struct View *v = tab_active_view(tab_active());
    set_none(&v->selection_end);
    return 0;
}

int visual_handle_key(struct KeyEvent *e) {
    struct View *v = tab_active_view(tab_active());
    assert(is_some(v->selection_end));

    if(e->modifier == 0) {
        switch(e->key) {
            case '\e':
                mode_change(M_Normal);
                return 0;

            case KC_ARRDOWN:
            case 'j': {
                view_move_cursor(v, 0,+1);
            } break;
            case KC_ARRUP:
            case 'k': {
                view_move_cursor(v, 0,-1);
            } break;
            case KC_ARRLEFT:
            case 'h': {
                view_move_cursor(v, -1,0);
            } break;
            case KC_ARRRIGHT:
            case 'l': {
                view_move_cursor(v, +1,0);
            } break;
        }
    }
    return 0;
}

static struct ModeInterface MODES[] = {
    (struct ModeInterface){
        .mode_str = "NOR",
        .handle_key = normal_handle_key,
        .on_enter = 0,
        .on_leave = 0,
    },
    (struct ModeInterface){
        .mode_str = "INS",
        .handle_key = insert_handle_key,
        .on_enter = insert_enter,
        .on_leave = insert_leave,
    },
    (struct ModeInterface){
        .mode_str = "WIN",
        .handle_key = window_handle_key,
        .on_enter = 0,
        .on_leave = 0,
    },
    (struct ModeInterface){
        .mode_str = "COM",
        .handle_key = command_handle_key,
        .on_enter = command_enter,
        .on_leave = command_leave,
    },
    (struct ModeInterface){
        .mode_str = "VIS",
        .handle_key = visual_handle_key,
        .on_enter = visual_enter,
        .on_leave = visual_leave,
    },
};

struct ModeInterface mode_current(void) {
    return MODES[MODE];
}

int mode_change(enum Mode mode) {
    int ret = 0;
    if(MODE != mode) {
        struct ModeInterface imode = mode_current();
        if(imode.on_leave) {
            ret = imode.on_leave();
            if(ret) return ret;
        }
        // change the mode
        MODE = mode;
        imode = mode_current();
        // call the setup function
        if(imode.on_enter) {
            ret = imode.on_enter();
            if(ret) return ret;
        }
    }
    return 0;
}

static size_t message_line_render_height(struct winsize *ws) {
    size_t msg_line_height = 1;
    if(MESSAGE.buff->lines.len) {
        struct Line *message_line = buffer_line_get(MESSAGE.buff, 0);
        msg_line_height += message_line->render_width / ws->ws_col;
    }
    return msg_line_height;
}

int message_line_render(struct winsize *ws, struct AbsoluteCursor *ac) {
    size_t msg_line_height = message_line_render_height(ws);
    ViewPort vp = {
        .width = ws->ws_col,
        .height = msg_line_height,
        .off_x = 0,
        .off_y = ws->ws_row - msg_line_height,
    };

    view_render(&MESSAGE, &vp, ws, ac);
    return 0;
}


int active_line_render(struct winsize *ws) {
    struct View *v = tab_active_view(tab_active());

    Style active_line_style = style_bg(style_new(), colour_vt(VT_GRA));
    // TODO(louis) compute render with first
    set_cursor_pos(0, ws->ws_row - 1 - message_line_render_height(ws));
    style_fmt(
            &active_line_style,
            STDOUT_FILENO,
            "%*c",
            ws->ws_col,
            ' ');

    set_cursor_pos(0, ws->ws_row - 2);
    style_fmt(
            &active_line_style,
            STDOUT_FILENO,
            "[%s] (%ld, %ld) %ld/%ld %ld",
            mode_current().mode_str,
            v->view_cursor.off_x + 1,
            v->view_cursor.off_y + 1,
            v->line_off + 1,
            v->buff->lines.len,
            tab_active()->active_window
            );

    return 0;
}

int editor_render(struct winsize *ws) {
    if(!RUNNING) return 0;
    write(STDOUT_FILENO, CUR_HIDE, sizeof(CUR_HIDE) -1);

    struct AbsoluteCursor ac = {
        .col = 1,
        .row = 1,
    };

    if(tabs_render(ws, &ac)) return -1;
    if(active_line_render(ws)) return -1;

    if(MESSAGE.buff->lines.len && MODE == M_Command) {
        assert(MESSAGE.buff->lines.len <= 1 && "commands should fit on one line");
        if(message_line_render(ws, &ac)) {
            write(STDOUT_FILENO, CUR_SHOW, sizeof(CUR_SHOW) -1);
            return -1;
        }
    } else {
        if(message_line_render(ws, 0)) {
            write(STDOUT_FILENO, CUR_SHOW, sizeof(CUR_SHOW) -1);
            return -1;
        }
    }
    set_cursor_pos(ac.col, ac.row);
    write(STDOUT_FILENO, CUR_SHOW, sizeof(CUR_SHOW) -1);
    return 0;
}

void editor_quit_all(void) {
    RUNNING = 0;
}

void editor_quit(void) {
    struct Tab *active_tab = tab_active();
    struct Window *active_window = tab_window_active(active_tab);
    window_close_view(active_window, -1);

    // remove the view
    if(active_window->view_stack.len == 0) {
        // if the window is empty, remove the window
        if(tab_remove_window(active_tab, -1)) {
            // if the tab is empty remove it
            tabs_remove(-1);
            // if all the tabs are removed, exit
            if(TABS.len == 0) {
                editor_quit_all();
            }
        }
    }
}

int path_expand(const char* path, char **out) {
    wordexp_t result;

    // expand path
    switch(wordexp(path, &result, WRDE_NOCMD)) {
        case WRDE_BADCHAR:
            return WRDE_BADCHAR;
        default:
            break;
    }

    if(result.we_wordc == 0) {
        size_t len = strlen(path);
        *out = xcalloc(len+1, 1);
        strcpy(*out, path);
        return 0;
    }

    size_t len = strlen(*result.we_wordv);
    *out = xcalloc(len+1, 1);
    strcpy(*out, *result.we_wordv);
    wordfree(&result);
    return 0;
}

void editor_write(const char *path) {
    struct Tab *active_tab = tab_active();
    struct Window *active_window = tab_window_active(active_tab);
    struct View *active_view = window_view_active(active_window);

    const char *true_path = path;

    switch(active_view->buff->in.ty) {
        case INPUT_SCRATCH:
            if(!path) {
                message_print("E: scratch buffers do not have a pre-defined path");
                return;
            }
            active_view->buff->in.ty = INPUT_FILE;
            active_view->buff->in.u.file.path = str_from_cstr(path);
            active_view->buff->in.u.file.fm = FM_RW;
            break;
        case INPUT_FILE:
            if(!path) {
                if(str_len(&active_view->buff->in.u.file.path)) {
                    true_path = str_as_cstr(&active_view->buff->in.u.file.path);
                } else {
                    assert(0 && "files should always have a path");
                }
            }
            break;
    }

    char *expanded = 0;
    if(path_expand(true_path, &expanded)) {
        message_print("E: invalid characters in path: '%s'", path);
        return;
    }

    if(buffer_dump(active_view->buff, expanded)) {
        char *error = strerror(errno);
        message_print("E: Unable to write to '%s': %s", expanded, error);
    } else {
        message_print("Written to '%s'", expanded);
    }
    xfree(expanded);
}

// Attempts to open a view, all errors, if any will be logged in the message line
int view_from_path(const char *path, enum FileMode fm, struct View *v) {
    struct Buffer *buff = calloc(1, sizeof(struct Buffer));

    char *expanded = 0;
    if(path_expand(path, &expanded)) {
        message_print("E: invalid characters in path: '%s'", path);
        return -1;
    }

    if(buffer_init_from_path(buff, expanded, fm)) {
        const char *error = strerror(errno);
        message_print("E: Unable to open '%s': %s", expanded, error);
        free(expanded);
        return -1;
    }

    free(expanded);
    *v = view_new(buff);
    return 0;
}

void editor_tabnew(const char *path, enum FileMode fm) {
    if(!path) {
        struct Tab *active_tab = tab_active();
        struct Window *active_window = tab_window_active(active_tab);
        tabs_push(tab_new(window_clone_shallow(active_window), 0));
        return;
    }

    struct View new_view = {0};

    if(view_from_path(path, fm, &new_view)) {
        return;
    }

    struct Window win = window_new();
    window_view_push(&win, new_view);
    tabs_push(tab_new(win, path));
}

void editor_split_open(const char *path, enum FileMode fm, enum SplitDir split) {
    struct Tab *active_tab = tab_active();
    struct Window *active_window = tab_window_active(active_tab);
    struct Window *new_win = xcalloc(1, sizeof(struct Window));

    if(!path) {
        *new_win = window_clone_shallow(active_window);
    } else {
        struct View new_view = {0};

        if(view_from_path(path, fm, &new_view)) {
            xfree(new_win);
            return;
        }
        window_view_push(new_win, new_view);
    }

    if(active_window->child) {
        new_win->child = active_window->child;
    }

    active_window->child = new_win;
    active_window->split_dir = split;

    return;
}

void editor_open(const char *path, enum FileMode fm) {
    assert(path && "TODO reload file if it has a path");

    struct Tab *active_tab = tab_active();
    struct Window *active_window = tab_window_active(active_tab);
    struct View *active_view = window_view_active(active_window);

    struct View new_view = {0};

    if(view_from_path(path, fm, &new_view)) {
        return;
    }

    view_free(active_view);
    *active_view = new_view;
}

void editor_init(void) {
    MESSAGE.buff = calloc(1, sizeof(struct Buffer));
    MESSAGE.options.no_line_num = 1;

    if(TABS.len == 0) {
        struct Buffer *buff = calloc(1, sizeof(struct Buffer));
        struct View view = view_new(buff);
        struct Window win = window_new();
        window_view_push(&win, view);
        struct Tab tab = tab_new(win, 0);
        tabs_push(tab);
    }
}

void editor_teardown(void) {
    vec_cleanup(&TABS);
    view_free(&MESSAGE);
}
