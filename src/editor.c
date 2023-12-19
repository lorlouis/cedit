#include "editor.h"
#include "vt.h"
#include "xalloc.h"
#include "str.h"
#include "commands.h"

#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <wctype.h>
#include <math.h>

#define TAB_WIDTH 4

struct winsize WS = {0};

int RUNNING = 1;

struct AbsoluteCursor {
    uint16_t col;
    uint16_t row;
};

// defaults to a scratch
static struct Buffer MESSAGE_BUFFER = {0};

struct View MESSAGE = {
    .buff = &MESSAGE_BUFFER,
    .options = {
        .no_line_num = 0,
    }
};

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

int filemode_save(
        enum FileMode fm,
        const char *path,
        struct Buffer *buff) {

    char *mode;

    switch(fm) {
        case FM_RW:
            mode = "w+";
            break;
        case FM_RO:
            errno = EBADF;
            return -1;
    }

    FILE *f = fopen(path, mode);
    if(!f) {
        // errno is already set
        return -1;
    }

    for(size_t i = 0; i < buff->lines_len; i++) {
        Str line = buff->lines[i];
        size_t ret = fwrite(str_as_cstr(&line), 1, str_cstr_len(&line), f);
        if(ret != str_cstr_len(&line)) {
            errno = ferror(f);
            fclose(f);
            return -1;
        }
        if(fwrite("\n", 1, 1, f) != 1) {
            errno = ferror(f);
            fclose(f);
            return -1;
        }
    }

    fclose(f);
    return 0;
}

int buffer_init_from_path(
        struct Buffer *buff,
        char *path,
        enum FileMode fm) {

    errno = 0;
    FILE *f = filemode_open(fm, path);
    if(!f) return -1;

    Str *lines = 0;
    size_t lines_cap = 0;
    size_t lines_len = 0;

    char *line=0;
    size_t cap=0;
    ssize_t len=0;
    errno = 0;
    while((len=getline(&line, &cap, f)) > 0) {
        if(lines_len >= lines_cap) {
            // TODO(louis) check for overflow
            lines_cap = lines_cap ? lines_cap * 2 : 1;
            lines = xrealloc(lines, lines_cap * sizeof(Str));
        }

        if(line[len-1] == '\n') {
            line[len-1] = '\0';
            len-=1;
        }

        lines[lines_len] = str_from_cstr_len(line, len+1);

        lines_len += 1;
    }
    free(line);

    if(len == -1 && ferror(f)) {
        errno = ferror(f);
        goto err;
    }

    buff->in.ty = INPUTTY_FILE;
    buff->in.u.file.fm = fm;
    buff->in.u.file.path = str_from_cstr(path);

    buff->lines = lines;
    buff->lines_cap = lines_cap;
    buff->lines_len = lines_len;
    buff->rc = 0;

    return 0;

    err:
        for(size_t i = 0; i < lines_len; i++) {
            str_free(lines + i);
        }
        free(lines);
        return -1;
}

// DO NOT USE DIRECTLY, USE `buffer_rc_dec`
void buffer_cleanup(struct Buffer *buff) {
    if(buff->lines) {
        for(size_t i = 0; i < buff->lines_len; i++) {
            str_free(buff->lines + i);
        }
        free(buff->lines);
    }

    switch(buff->in.ty) {
        case INPUTTY_SCRATCH:
            break;
        case INPUTTY_FILE:
            str_free(&buff->in.u.file.path);
            break;
    }
    memset(buff, 0, sizeof(struct Buffer));
    return;
}

int write_escaped(Style *base_style, int fd, const Str *line, size_t len) {
    size_t off = 0;
    int count = 0;
    Style substitution = style_new();
    if(base_style) {
        substitution = style_fg(*base_style, colour_vt(VT_GRA));
    }
    style_begin(base_style, fd);
    while(off < len) {
        utf32 c = 0;
        assert(str_get_char(line, off, &c) != -1);

        wint_t wc = utf32_to_wint(c);
        if(wc == L'\n' || wc == 0) break;
        if(wc == (wint_t)-1) {
            style_reset(fd);
            return -1;
        }

        // could be wide null (unlikely but still)
        assert(wc && "null in middle of the line");

        if(wc == L'\t') {
            Style style = {0};
            style = style_fg(style, colour_vt(VT_GRA));

            // exit current style
            style_reset(fd);

            style_fmt(&substitution, fd, "%s", ">---");
            // go back to previous style
            style_begin(base_style, fd);

            count += TAB_WIDTH;
        } else if (iswprint(wc)) {
            write(fd, str_tail_cstr(line, off), utf32_len_utf8(c));
            count += utf32_len_utf8(c);
        }
        off += 1;
    }
    style_reset(fd);
    return count;
}

uint16_t viewport_viewable_width(const struct ViewPort *vp, const struct winsize *ws) {
    uint16_t real_width = vp->width;
    if(vp->width + vp->off_x > ws->ws_col) {
        real_width = vp->width - ((vp->width + vp->off_x) - ws->ws_col);
    }
    return real_width;
}

uint16_t viewport_viewable_height(const struct ViewPort *vp, const struct winsize *ws) {
    uint16_t real_height = vp->height;
    if(vp->height + vp->off_y >= ws->ws_row) {
        real_height = vp->height - ((vp->height + vp->off_y) - ws->ws_row);
    }
    return real_height;
}

uint16_t viewport_width_clamp(const struct ViewPort *vp, const struct winsize *ws, uint16_t v) {
    if(v < vp->off_x) return vp->off_x;
    if(v > vp->off_x + ws->ws_col -1) return vp->off_x + ws->ws_col -1;
    return v;
}

uint16_t viewport_height_clamp(const struct ViewPort *vp, const struct winsize *ws, uint16_t v) {
    if(v < vp->off_y) return vp->off_y;
    if(v > vp->off_y + ws->ws_row-1) return vp->off_y + ws->ws_row-1;
    return v;
}

void view_clear(struct View *v) {
    v->line_off = 0;
    v->view_cursor.off_x = 0;
    v->view_cursor.off_y = 0;
    for(size_t i = 0; i < v->buff->lines_len; i++) {
        str_clear(v->buff->lines + i);
    }
}

void view_free(struct View *v) {
    buffer_rc_dec(v->buff);
}

int view_write(struct View *v, const char *restrict s, size_t len) {

    size_t off = 0;

    size_t line_idx = v->view_cursor.off_y;

    if(!v->buff->lines) {
        v->buff->lines = calloc(1, sizeof(Str));
        v->buff->lines_cap = 1;
        v->buff->lines_len = 1;
    }

    Str *line = v->buff->lines + line_idx;
    char *end_of_line = 0;
    size_t end_of_line_size = 0;
    size_t source_line = v->view_cursor.off_y;

    // save end of line and null terminator
    end_of_line_size = str_cstr_len(line) - str_get_char_byte_idx(line, v->view_cursor.off_x);
    end_of_line = xcalloc(end_of_line_size, sizeof(char));

    memcpy(end_of_line, str_as_cstr(line) + str_get_char_byte_idx(line, v->view_cursor.off_x), end_of_line_size);

    while(off < len) {
        size_t start = off;
        while(s[off] != '\n' && s[off] != '\0') off++;
        size_t size = off - start;
        size_t cursor = str_get_char_byte_idx(line, v->view_cursor.off_x);

        size_t original_len = str_len(line);

        // copy the new block on top of the old stuff
        str_insert_at(line, v->view_cursor.off_x, s + start, size);

        size_t new_len = str_len(line);

        v->view_cursor.off_x += new_len - original_len;

        if(s[off] == '\n') {
            if(cursor <= str_len(line)) {
                str_trunc(line, cursor);
            }
            off++;
            // add 1 more line
            if(1+v->buff->lines_len >= v->buff->lines_cap) {
                v->buff->lines = xrealloc(v->buff->lines, v->buff->lines_cap * 2 * sizeof(Str));
                v->buff->lines_cap = v->buff->lines_cap * 2;
            }

            size_t line_cursor = v->view_cursor.off_y;
            // shift the lines 1 down
            memmove(v->buff->lines + line_cursor + 1,
                    v->buff->lines + line_cursor,
                    (v->buff->lines_len - line_cursor) * sizeof(Str));

            v->buff->lines_len += 1;

            line = v->buff->lines + line_cursor+1;
            *line = str_new();

            v->view_cursor.off_y += 1;

            v->view_cursor.off_x = 0;
        }
    }
    if(end_of_line_size) {
        // write end of line at the end of the current cursor line
        // if the line changed
        if(source_line != v->view_cursor.off_y) {
            str_push(line, end_of_line, end_of_line_size);
        }
    }
    free(end_of_line);
    return 0;
}

void view_set_cursor(struct View *v, size_t x, size_t y) {
    if(!v->buff || !v->buff->lines) return;
    v->view_cursor.off_y = y < v->buff->lines_len ? y : v->buff->lines_len-1;
    Str *l = v->buff->lines + v->view_cursor.off_y;

    v->view_cursor.off_x = x < str_len(l) ? x : str_len(l);
}

void view_move_cursor(struct View *v, ssize_t off_x, ssize_t off_y) {
    ssize_t new_x = (ssize_t)v->view_cursor.off_x + off_x;
    if(new_x < 0) new_x = 0;
    ssize_t new_y = (ssize_t)v->view_cursor.off_y + off_y;
    if(new_y < 0) new_y = 0;

    view_set_cursor(v, new_x, new_y);
}

int view_render(struct View *v, struct ViewPort *vp, const struct winsize *ws, struct AbsoluteCursor *ac) {
    struct ViewPort *real_vp = vp;

    Style view_bg = style_new();

    Style num_text = style_fg(view_bg, colour_vt(VT_BLU));

    if(v->vp) {
        real_vp = v->vp;
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
        num_width = ceil(log10((double)v->buff->lines_len)) + 1;
        if(num_width < 2) num_width = 2;
    }

    size_t *extra_render_line_per_line = xcalloc(real_height, sizeof(size_t));
    size_t line_off = 0;
    uint16_t cursor_line_off = 0;
    size_t i = 0;
    for(;
            i + v->line_off < v->buff->lines_len && i + line_off < real_height;
            i++) {
        Str l = v->buff->lines[i+v->line_off];

        size_t char_off = 0;
        size_t extra_render_line_count = 0;
        while(i + line_off + extra_render_line_count < real_height) {
            ssize_t len = 0;
            size_t take_width = real_width - num_width;

            if(char_off < str_len(&l)) {
                Str tail = str_tail(&l, char_off);
                len = take_cols(&tail, &take_width, TAB_WIDTH);
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

            Str tail = str_tail(&l, char_off);
            write_escaped(&view_bg, STDOUT_FILENO, &tail, len);
            // fill the rest of the line, useful when views overlap
            int fill = real_width-take_width-num_width;
            if(fill) {
                style_fmt(&view_bg, STDERR_FILENO, "%*s", fill, " ");
            }

            // reached the end of the line
            if((size_t)len + char_off >= str_len(&l)) break;

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

    if(ac && v->buff->lines) {
        Str *line = v->buff->lines + v->view_cursor.off_y;
        Str line_until_col = str_head(line, v->view_cursor.off_x+1);
        uint16_t col = count_cols(&line_until_col, 4);
        assert(col != (uint16_t)-1);
        uint16_t row = v->view_cursor.off_y - v->line_off + cursor_line_off;

        if(col >= real_vp->width - num_width) {
            int16_t diff = col - (real_vp->width - num_width);
            size_t len = 0;
            while(diff >= 0) {
                size_t take_width = real_vp->width - num_width;
                Str tail = str_tail(line, len);
                Str tail_head = str_head(&tail, v->view_cursor.off_x - len + 1);
                //size_t new_len = take_cols(str_as_cstr(line) + len, v->view_cursor.off_x - len, &take_width, 4);
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
            return view_render(v, vp, ws, ac);
        }
        else {
            // TODO check col here, it looks like this is where the é bug happens
            ac->col = viewport_width_clamp(real_vp, ws, col + num_width + real_vp->off_x);
            ac->row = viewport_height_clamp(real_vp, ws, row + real_vp->off_y);
        }
    } else if (ac && !v->buff->lines) {
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
        return 1;
    }
    buff->rc -= 1;
    return 0;
}

void tab_free(struct Tab *t) {
    str_free(&t->name);
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

void window_free(struct Window *w) {
}

int window_render(struct Window *w, struct ViewPort *vp, const struct winsize *ws, struct AbsoluteCursor *ac) {
    Style line_style = {0};
    line_style = style_fg(line_style, colour_vt(VT_YEL));

    int is_odd = 0;
    struct ViewPort self_vp = *vp;

    if(w->child) {
        struct ViewPort sub_vp;
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
        window_render(w->child, &sub_vp, ws, ac);
        // render subwindow(s)
        if(tab_active_window(tab_active()) == w) {
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
        self_vp = sub_vp;
    } else {
        if(tab_active_window(tab_active()) == w) {
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
    struct Window *w = tab->w;
    // select current window
    for(size_t i = 0; i < idx; i++) {
        w = w->child;
    }
    return w;
}

struct Window* tab_active_window(struct Tab *tab) {
    return tab_get_window(tab, tab->active_window);
}

struct View* tab_active_view(struct Tab *tab) {
    struct Window *w = tab_active_window(tab);
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
    struct Window *cur_window = tab_active_window(cur_tab);
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

// TODO(louis) free tabs

int tabs_render(struct winsize *ws, struct AbsoluteCursor *ac) {
    Style unselected = style_bg(style_new(), colour_vt(VT_GRA));
    Style selected = style_bg(style_new(), colour_vt(VT_BLK));
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
        }
        if(!str_is_empty(&tab_get(i)->name)) {
            size_t cols = 10;
            int len = take_cols(&tab_get(i)->name, &cols, TAB_WIDTH);
            assert(len >= 0 && "error when computing len");
            dprintf(STDOUT_FILENO, "[%.*s]", len, str_as_cstr(&tab_get(i)->name));
            sum+=cols+2;
        } else {
            dprintf(STDOUT_FILENO, "[%.4ld]", i);
            sum+=6;
        }
        if(i == ACTIVE_TAB) {
            style_reset(STDOUT_FILENO);
            style_begin(&unselected, STDOUT_FILENO);
        }
    }
    style_reset(STDOUT_FILENO);
    struct ViewPort vp = {
        // some space to put the status line
        .height = ws->ws_row - 3,
        .width = ws->ws_col,
        // screen coordinate start at 1
        .off_x = 0,
        // skip the tabs
        .off_y = 1,
    };

    window_render(tab_active()->w, &vp, ws, ac);

    return 0;
}

// DO NOT USE DIRECTLY, use mode_* functions
static enum Mode MODE = M_Normal;

int normal_handle_key(struct KeyEvent *e) {
    struct View *v = tab_active_view(tab_active());
    if(e->modifier == 0) {
        switch(e->key) {
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

int view_erase(struct View *v) {
    size_t cursor = v->view_cursor.off_x;

    Str *line = v->buff->lines + v->view_cursor.off_y;
    if(cursor > 0) {
        str_remove(line, cursor-1, cursor-1);
        v->view_cursor.off_x -= 1;
    } else if(v->view_cursor.off_y > 0) {
        size_t cursor_line = v->view_cursor.off_y;
        Str *prev_line = v->buff->lines + v->view_cursor.off_y -1;
        // move cursor to end of previous line
        v->view_cursor.off_x = str_len(prev_line);
        str_push(prev_line, str_as_cstr(line), str_cstr_len(line));

        if(cursor_line+1 <= v->buff->lines_len) {
            str_free(line);
            memmove(v->buff->lines + cursor_line,
                    v->buff->lines + cursor_line+1,
                    (v->buff->lines_len - cursor_line) * sizeof(Str));
        }
        v->buff->lines_len -= 1;
        v->view_cursor.off_y -= 1;
    }
    return 0;
}

int insert_handle_key(struct KeyEvent *e) {
    struct View *v = tab_active_view(tab_active());
    if(e->key == '\e') {
        mode_change(M_Normal);
        return 0;
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

void view_next(void) {
    struct Tab *cur_tab = tab_active();
    struct Window *cw = tab_active_window(cur_tab);
    if(cw->view_stack.len > 0 && cw->active_view < cw->view_stack.len-1) {
        cw->active_view += 1;
    } else {
        cw->active_view = 0;
    }
}

void view_prev(void) {
    struct Tab *cur_tab = tab_active();
    struct Window *cw = tab_active_window(cur_tab);
    if(cw->active_view > 0) {
        cw->active_view -= 1;
    } else if(cw->view_stack.len > 0){
        cw->active_view = cw->view_stack.len-1;
    }
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
    buffer_cleanup(&MESSAGE_BUFFER);
    MESSAGE.line_off = 0;
    MESSAGE.view_cursor.off_x = 0;
    MESSAGE.view_cursor.off_y = 0;
    view_write(&MESSAGE, ":", 1);
    return 0;
}

int command_leave(void) {
    buffer_cleanup(&MESSAGE_BUFFER);
    return 0;
}

int command_handle_key(struct KeyEvent *e) {
    // TODO(louis) handle command buffer here
    if(e->modifier == 0) {
        if(e->key == '\e') {
            mode_change(M_Normal);
            return 0;
        } else if(e->key == KC_BACKSPACE) {
            view_erase(&MESSAGE);
            return 0;
        } else if(e->key == '\n') {
            if(MESSAGE_BUFFER.lines) {
                Str command = str_clone(MESSAGE_BUFFER.lines);
                view_clear(&MESSAGE);
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
            view_write(&MESSAGE, bytes, len);
        } break;
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
        .on_enter = 0,
        .on_leave = 0,
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
        .on_leave = 0,
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

int active_line_render(struct winsize *ws) {
    struct View *v = tab_active_view(tab_active());

    Style active_line_style = style_bg(style_new(), colour_vt(VT_GRA));
    // TODO(louis) compute render with first
    set_cursor_pos(0, ws->ws_row - 2);
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
            v->buff->lines_len,
            tab_active()->active_window
            );

    return 0;
}

int message_line_render(struct winsize *ws, struct AbsoluteCursor *ac) {
    struct ViewPort vp = {
        .width = ws->ws_col,
        .height = 1,
        .off_x = 0,
        .off_y = ws->ws_row-1,
    };

    view_render(&MESSAGE, &vp, ws, ac);
    return 0;
}

int editor_render(struct winsize *ws) {
    struct AbsoluteCursor ac = {
        .col = 1,
        .row = 1,
    };

    write(STDOUT_FILENO, CLS, sizeof(CLS));
    if(tabs_render(ws, &ac)) return -1;
    if(active_line_render(ws)) return -1;

    if(MESSAGE_BUFFER.lines && MODE == M_Command) {
        if(message_line_render(ws, &ac)) return -1;
    } else {
        if(message_line_render(ws, 0)) return -1;
    }
    set_cursor_pos(ac.col, ac.row);
    return 0;
}

void editor_teardown(void) {
    vec_cleanup(&TABS);
    return;
}
