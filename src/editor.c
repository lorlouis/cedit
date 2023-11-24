#include "editor.h"
#include "vt.h"
#include "xalloc.h"
#include "str.h"

#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <wctype.h>
#include <math.h>

#define TAB_WIDTH 4

struct winsize WS = {0};

struct AbsoluteCursor {
    uint16_t col;
    uint16_t row;
};

struct {
    Str msg;
    size_t cursor_position;
} MESSAGE_LINE = {0};

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
        struct Line line = buff->lines[i];
        size_t ret = fwrite(line.buf, 1, line.len, f);
        if(ret != line.len) {
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

    struct Line *lines = 0;
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
            lines = xrealloc(lines, lines_cap * sizeof(struct Line));
        }

        if(line[len-1] == '\n') {
            line[len-1] = '\0';
            len-=1;
        }

        lines[lines_len] = (struct Line) {
            .buf = line,
            .cap = cap,
            .len = len,
        };


        // makes getline allocate a new line
        line = 0;
        cap = 0;
        len = 0;

        lines_len += 1;
    }

    if(len == -1 && ferror(f)) {
        errno = ferror(f);
        goto err;
    }

    buff->in.ty = INPUTTY_FILE;
    buff->in.u.file.fm = fm;
    buff->in.u.file.path = path;

    buff->lines = lines;
    buff->lines_cap = lines_cap;
    buff->lines_len = lines_len;
    buff->rc = 0;

    return 0;

    err:
        for(size_t i = 0; i < lines_len; i++) {
            free(lines[i].buf);
        }
        free(lines);
        return -1;
}

void buffer_cleanup(struct Buffer *buff) {

    for(size_t i = 0; i < buff->lines_len; i++) {
        free(buff->lines[i].buf);
    }
    free(buff->lines);

    switch(buff->in.ty) {
        case INPUTTY_SCRATCH:
            break;
        case INPUTTY_FILE:
            free(buff->in.u.file.path);
            break;
    }
}

int write_escaped(int fd, const char *restrict line, size_t len) {
    size_t off = 0;
    int count = 0;
    while(off < len && *(line+off) && *(line+off) != '\n') {
        wchar_t c;
        int read = mbtowc(&c, line + off, len);
        if(read == -1) {
            return -1;
        }

        // could be wide null (unlikely but still)
        assert(read && "null in middle of the line");

        if(c == L'\t') {
            char tab_vis[] = CSI"90m" ">---" CSI"39;49m";
            if(write(fd, tab_vis, sizeof(tab_vis)) == -1) return -1;
            count += TAB_WIDTH;
        } else if (iswprint(c)) {
            if(write(fd, line+off, read) == -1) return -1;
            count += read;
        }
        off += read;
    }
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

int view_write(struct View *v, const char *restrict s, size_t len) {

    size_t off = 0;

    size_t line_idx = v->view_cursor.off_y;

    struct Line *line = v->buff->lines + line_idx;
    char *end_of_line = 0;
    size_t end_of_line_size = 0;

    while(off < len) {
        size_t start = off;
        while(s[off] != '\n' && s[off] != '\0') off++;
        size_t size = off - start;
        size_t cursor = v->view_cursor.off_x;
        if(size + cursor >= line->cap) {
            size_t new_cap = line->cap * 2;
            while(new_cap < size + line->len) {
                new_cap = new_cap * 2;
            }
            line->buf = xrealloc(line->buf, new_cap);
            line->cap = new_cap;
        }

        // TODO(louis) copy end of line to it's own buffer and append it
        // to the current line

        if(!end_of_line) {
            // save end of line and null terminator
            end_of_line_size = line->len - cursor + 1;
            end_of_line = xcalloc(end_of_line_size, sizeof(char));
            memcpy(end_of_line, line->buf + cursor, end_of_line_size);
        }

        // copy the new block on top of the old stuff
        memcpy(line->buf + cursor, s + start, size);
        // update the size
        line->len = cursor + size;
        // add null terminator
        line->buf[line->len] = '\0';

        v->view_cursor.off_x = line->len;

        if(s[off] == '\n') {
            off++;
            // add 1 more line
            if(1+v->buff->lines_len >= v->buff->lines_cap) {
                v->buff->lines = xrealloc(v->buff->lines, v->buff->lines_cap * 2 * sizeof(struct Line));
                v->buff->lines_cap = v->buff->lines_cap * 2;
            }

            size_t line_cursor = v->view_cursor.off_y;
            // shift the lines 1 down
            memmove(v->buff->lines + line_cursor + 1,
                    v->buff->lines + line_cursor,
                    (v->buff->lines_len - line_cursor) * sizeof(struct Line));

            v->buff->lines_len += 1;

            line = v->buff->lines + line_cursor+1;
            line->buf = xcalloc(16, sizeof(char));
            line->cap = 16;
            line->len = 0;

            v->view_cursor.off_y += 1;

            v->view_cursor.off_x = 0;
        }
    }
    // write end of line at the end of the current cursor line
    if(line->len + end_of_line_size >= line->cap) {
        size_t new_cap = line->cap * 2;
        while(new_cap < end_of_line_size + line->len) {
            new_cap = new_cap * 2;
        };
        line->buf = xrealloc(line->buf, new_cap);
        line->cap = new_cap;
    }
    memcpy(line->buf + line->len, end_of_line, end_of_line_size);
    line->len += end_of_line_size-1;
    line->buf[line->len] = '\0';
    free(end_of_line);
    return 0;
}

void view_set_cursor(struct View *v, size_t x, size_t y) {
    if(!v->buff || !v->buff->lines) return;
    v->view_cursor.off_y = y < v->buff->lines_len ? y : v->buff->lines_len-1;
    struct Line *l = v->buff->lines + v->view_cursor.off_y;

    v->view_cursor.off_x = x < l->len ? x : l->len;
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
            i + v->line_off < v->buff->lines_len && i + line_off <= real_height;
            i++) {
        struct Line l = v->buff->lines[i+v->line_off];

        size_t char_off = 0;
        size_t extra_render_line_count = 0;
        while(i + line_off + extra_render_line_count <= real_height) {
            ssize_t len = 0;
            size_t take_width = real_width - num_width;

            if(char_off <= l.len) {
                len = take_cols(char_off + l.buf, l.len - char_off, &take_width, TAB_WIDTH);
            }

            assert(len >= 0 && "bad string");
            assert(l.buf[char_off+len] != '\n' && "newline in middle of line");

            set_cursor_pos(real_vp->off_x, real_vp->off_y+i+line_off + extra_render_line_count);

            if (num_width) {
                dprintf(
                    STDOUT_FILENO,
                    "%s%*ld%s ",
                    CSI "90m",
                    num_width - 1,
                    i + v->line_off + 1,
                    CSI "39;49m"
                );
            }

            write_escaped(STDOUT_FILENO, l.buf + char_off, len);
            // fill the rest of the line, useful when views overlap
            int fill = real_width-take_width-num_width;
            if(fill) {
                dprintf(STDOUT_FILENO, "%*s", fill, "~");
            }

            // reached the end of the line
            if((size_t)len + char_off >= l.len) break;

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

    for(int j = i + line_off; j <= real_height; j++) {
        set_cursor_pos(real_vp->off_x, real_vp->off_y+j);
        if (num_width) {
            dprintf(
                STDOUT_FILENO,
                "%s%*c%s ",
                CSI "90m",
                num_width - 1,
                '~',
                CSI "39;49m"
            );
        }
        dprintf(STDOUT_FILENO, "%*s", real_width-num_width, " ");
    }

    if(ac && v->buff->lines) {
        struct Line *line = v->buff->lines + v->view_cursor.off_y;
        uint16_t col = count_cols(line->buf, v->view_cursor.off_x, 4);
        uint16_t row = v->view_cursor.off_y - v->line_off + cursor_line_off;

        if(col >= real_vp->width - num_width) {
            int16_t diff = col - (real_vp->width - num_width);
            size_t len = 0;
            while(diff >= 0) {
                size_t take_width = real_vp->width - num_width;
                size_t new_len = take_cols(line->buf + len, v->view_cursor.off_x - len, &take_width, 4);
                row += 1;
                col -= take_width;
                diff -= take_width;
                len = new_len;
            }
        }

        if(row > real_vp->height) {
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

struct Tab *TABS = 0;
size_t TABS_LEN = 0;
size_t TABS_CAP = 0;
size_t ACTIVE_TAB = 0;

int window_render(struct Window *w, struct ViewPort *vp, const struct winsize *ws, struct AbsoluteCursor *ac) {
    struct ViewPort self_vp = *vp;
    if(w->child) {
        struct ViewPort sub_vp;
        switch(w->split_dir) {
            case SD_Vertical: {
                // Look mah! No npm
                int is_odd = (self_vp.width-1) % 2;
                int is_even = !is_odd;
                self_vp.width = (self_vp.width-1) / 2;
                sub_vp = self_vp;
                sub_vp.off_x += self_vp.width+is_odd;
                self_vp.width -= is_even;

                // render split line
                for(int i = 0; i <= self_vp.height; i++) {
                    set_cursor_pos(self_vp.width + vp->off_x, i + vp->off_y);
                    dprintf(STDOUT_FILENO, "%s", CSI"90m" "|" CSI"39;49m");
                }
            } break;
            case SD_Horizontal: {
                int is_odd = (self_vp.height-1) % 2;
                int is_even = !is_odd;
                self_vp.height = (self_vp.height-1) / 2;
                sub_vp = self_vp;
                sub_vp.off_y += self_vp.height+1+is_odd;
                self_vp.height -= is_even;

                // render split line
                for(int i = 0; i <= self_vp.width; i++) {
                    set_cursor_pos(i + vp->off_x, vp->off_y+self_vp.height+1);
                    dprintf(STDOUT_FILENO, "%s", CSI"90m" "-" CSI"39;49m");
                }

            } break;
        }
        window_render(w->child, &sub_vp, ws, ac);
        // render subwindow(s)
        if(tab_active_window(TABS + ACTIVE_TAB) == w) {
            // render self window with cursor
            for(size_t i = 0; i < w->view_stack_len; i++) {
                if(i == w->active_view) {
                    view_render(w->view_stack + i, &self_vp, ws, ac);
                } else {
                    view_render(w->view_stack + i, &self_vp, ws, 0);
                }
            }
        } else {
            // render self window
            for(size_t i = 0; i < w->view_stack_len; i++) {
                view_render(w->view_stack + i, &self_vp, ws, 0);
            }
        }
        self_vp = sub_vp;
    } else {
        if(tab_active_window(TABS + ACTIVE_TAB) == w) {
            // render self window with cursor
            for(size_t i = 0; i < w->view_stack_len; i++) {
                if(i == w->active_view) {
                    view_render(w->view_stack + i, &self_vp, ws, ac);
                } else {
                    view_render(w->view_stack + i, &self_vp, ws, 0);
                }
            }
        } else {
            // render self window
            for(size_t i = 0; i < w->view_stack_len; i++) {
                view_render(w->view_stack + i, &self_vp, ws, 0);
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
    return w->view_stack + w->active_view;
}

int tabs_prev(void) {
    if(ACTIVE_TAB > 0) {
        ACTIVE_TAB -= 1;
    }
    return ACTIVE_TAB;
}

int tabs_next(void) {
    if(ACTIVE_TAB >= TABS_LEN) return ACTIVE_TAB;
    ACTIVE_TAB += 1;
    return ACTIVE_TAB;
}

int tabs_win_select(enum Direction dir) {
    struct Tab *cur_tab = TABS + ACTIVE_TAB;
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

int tabs_push(struct Tab tab) {
    if(TABS_LEN == TABS_CAP) {
        TABS_CAP = TABS_CAP > 0 ? TABS_CAP * 2 : 1;
        TABS = xrealloc(TABS, TABS_CAP * sizeof(struct Tab));
    }
    TABS[TABS_LEN] = tab;
    TABS_LEN += 1;
    return TABS_LEN;
}

int tabs_render(struct winsize *ws, struct AbsoluteCursor *ac) {
    size_t sum = 0;
    // render top bar
    set_cursor_pos(0,0);
    for(size_t i = 0; i < TABS_LEN; i++) {
        if(sum + 6 >= ws->ws_col) break;
        if(i == ACTIVE_TAB) {
            dprintf(STDOUT_FILENO, INV);
        }
        if(TABS[i].name) {
            size_t name_len = strlen(TABS[i].name);
            size_t cols = 10;
            int len = take_cols(TABS[i].name, name_len, &cols, TAB_WIDTH);
            assert(len >= 0 && "error when computing len");
            dprintf(STDOUT_FILENO, "[%.*s]", len, TABS[i].name);
            sum+=cols+2;
        } else {
            dprintf(STDOUT_FILENO, "[%.4ld]", i);
            sum+=6;
        }
        if(i == ACTIVE_TAB) {
            dprintf(STDOUT_FILENO, RESET);
        }
    }
    struct ViewPort vp = {
        // some space to put the status line
        .height = ws->ws_row - 4,
        .width = ws->ws_col,
        // screen coordinate start at 1
        .off_x = 0,
        // skip the tabs
        .off_y = 1,
    };

    window_render(TABS[ACTIVE_TAB].w, &vp, ws, ac);

    return 0;
}

// DO NOT USE DIRECTLY, use mode_* functions
static enum Mode MODE = M_Normal;

int normal_handle_key(struct KeyEvent *e) {
    struct View *v = tab_active_view(TABS + ACTIVE_TAB);
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

int insert_handle_key(struct KeyEvent *e) {
    struct View *v = tab_active_view(TABS + ACTIVE_TAB);
    if(e->key == '\e') {
        mode_change(M_Normal);
        return 0;
    } else if(e->key == KC_BACKSPACE) {
        size_t cursor = v->view_cursor.off_x;
        struct Line *line = v->buff->lines + v->view_cursor.off_y;
        if(cursor > 0) {
            memmove(line->buf + cursor -1, line->buf + cursor, line->len - 1);
            line->len -= 1;
            v->view_cursor.off_x -= 1;
        } else if(v->view_cursor.off_y > 0) {
            size_t cursor_line = v->view_cursor.off_y;
            struct Line *prev_line = v->buff->lines + v->view_cursor.off_y -1;
            // move cursor to end of previous line
            v->view_cursor.off_x = prev_line->len;
            if(line->len > 0) {
                // cursor must be 0
                if(prev_line->cap < line->len + prev_line->len) {
                    size_t new_size = prev_line->cap * 2;
                    while(new_size < line->len + prev_line->len) {
                        new_size *= 2;
                    }
                    prev_line->buf = xrealloc(prev_line->buf, new_size);
                    prev_line->cap = new_size;
                }

                // copy null terminator
                memcpy(prev_line->buf + prev_line->len, line->buf, line->len+1);
                prev_line->len += line->len;
            }
            if(cursor_line+1 <= v->buff->lines_len) {
                free(line->buf);
                memmove(v->buff->lines + cursor_line,
                        v->buff->lines + cursor_line+1,
                        (v->buff->lines_len - cursor_line) * sizeof(struct Line));
            }
            v->buff->lines_len -= 1;
            v->view_cursor.off_y -= 1;
        }

        return 0;
    } else {
        int len = check_utf8_start(*(char *)&e->key);
        view_write(v, (char *)&e->key, len);
    }

    return 0;
}

void view_next(void) {
    struct Tab *cur_tab = TABS + ACTIVE_TAB;
    struct Window *cw = tab_active_window(cur_tab);
    if(cw->view_stack_len > 0 && cw->active_view < cw->view_stack_len-1) {
        cw->active_view += 1;
    } else {
        cw->active_view = 0;
    }
}

void view_prev(void) {
    struct Tab *cur_tab = TABS + ACTIVE_TAB;
    struct Window *cw = tab_active_window(cur_tab);
    if(cw->active_view > 0) {
        cw->active_view -= 1;
    } else if(cw->view_stack_len > 0){
        cw->active_view = cw->view_stack_len-1;
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
    str_clear(&MESSAGE_LINE.msg);
    str_push(&MESSAGE_LINE.msg, ":", 1);
    MESSAGE_LINE.cursor_position = 1;
    return 0;
}

int command_leave(void) {
    str_clear(&MESSAGE_LINE.msg);
    MESSAGE_LINE.cursor_position = 0;
    return 0;
}

int command_handle_key(struct KeyEvent *e) {
    // TODO(louis) handle command buffer here
    if(e->modifier == 0) {
        if(e->key == '\e') {
            mode_change(M_Normal);
            return 0;
        }

    }
    switch(e->key) {
        case '\e': {
        } break;
        default:
            str_push(&MESSAGE_LINE.msg, ":", 1);
            break;
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
        .on_leave = command_leave,
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
    set_cursor_pos(0, ws->ws_row - 2);

    struct View *v = tab_active_view(TABS + ACTIVE_TAB);

    dprintf(STDOUT_FILENO,
            "[%s] (%ld, %ld) %ld/%ld %ld",
            mode_current().mode_str,
            v->view_cursor.off_x + 1,
            v->view_cursor.off_y + 1,
            v->line_off + 1,
            v->buff->lines_len,
            TABS[ACTIVE_TAB].active_window);

    return 0;
}

int message_line_render(struct winsize *ws, struct AbsoluteCursor *ac) {
    set_cursor_pos(0, ws->ws_row -1);
    if(MESSAGE_LINE.msg.len) {
        int len = MESSAGE_LINE.msg.len;
        dprintf(STDOUT_FILENO, "%.*s", len, MESSAGE_LINE.msg.buf);
    }

    if(MESSAGE_LINE.cursor_position) {
        ac->row = ws->ws_row -1;
        ac->col = MESSAGE_LINE.cursor_position;
    }
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
    if(message_line_render(ws, &ac)) return -1;
    set_cursor_pos(ac.col, ac.row);
    return 0;
}
