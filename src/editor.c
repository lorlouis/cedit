#include "editor.h"
#include "buffer.h"
#include "vt.h"
#include "highlight.h"
#include "xalloc.h"
#include "str.h"
#include "commands.h"
#include "config.h"
#include "line.h"
#include "exec.h"

#include <regex.h>
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

const char *FILEMODE_REPR[2] = {"", "[RO]"};

int RUNNING = 1;

struct View MESSAGE = {
    .buff = 0,
    .options = {
        .no_line_num = 0,
    }
};

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
        va_end(args);
        return -1;
    }
    s_size = (size_t)ret;

    view_write(&MESSAGE, formatted, s_size);
    free(formatted);
    va_end(args);

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
            style_fmt(base_style, fd, "%*s", utf32_len_utf8(c), char_buffer);
        }
        count += utf32_width(c);
    }

    return count;
}

int write_escaped(Style *base_style, const struct Line *line, size_t off, size_t len) {
    int count = 0;
    int ret;
    for(size_t i = off; i < len+off; i++) {
        utf32 c = 0;
        assert(str_get_char(&line->text, i, &c) != -1);

        // could be wide null (unlikely but still)
        assert(c && "null in middle of the line");
        assert(c != L'\n' && "new line in middle of the line");

        Style s = *base_style;
        if(i < line->style_ids.len) {
            Style *cur = style_find_by_id(*VEC_GET(char, &line->style_ids, i));
            if(cur) {
                s = style_merge(s, *cur);
            }
        }

        ret = write_char_escaped(&s, c, STDOUT_FILENO);
        if(ret == -1) return ret;
        count += ret;
    }
    return count;
}

static int view_write_escaped(
        Style *base_style,
        struct View *v,
        size_t line_idx,
        size_t off,
        size_t len,
        Style *highlight,
        struct ViewSelection *vs) {

    if(!vs) return write_escaped(base_style, buffer_line_get(v->buff, line_idx), off, len);

    int count = 0;
    int ret;
    struct Line *line = buffer_line_get(v->buff, line_idx);
    for(size_t i = off; i < len+off; i++) {
        utf32 c = 0;
        assert(str_get_char(&line->text, i, &c) != -1);

        // could be wide null (unlikely but still)
        assert(c && "null in middle of the line");
        assert(c != L'\n' && "new line in middle of the line");

        Style s = *base_style;

        if(i < line->style_ids.len) {
            Style *cur = style_find_by_id(*VEC_GET(char, &line->style_ids, i));
            if(cur) {
                s = style_merge(s, *cur);
            }
        }

        if(view_selection_position_selected(vs, line_idx, i)) s = style_merge(s, *highlight);

        ret = write_char_escaped(&s, c, STDOUT_FILENO);
        if(ret == -1) return ret;
        count += ret;
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
    v->first_line_char_off = 0;
    v->view_cursor.off_x = 0;
    v->view_cursor.off_y = 0;
    for(size_t i = 0; i < v->buff->lines.len; i++) {
        line_free(buffer_line_get(v->buff, i));
    }
    v->buff->lines.len = 0;
}

void view_free(struct View *v) {
    if(v->buff) buffer_rc_dec(v->buff);
}

int view_write(struct View *v, const char *restrict s, size_t len) {
    if(len == 0) return 0;
    v->buff->dirty = 1;

    size_t line_idx = v->view_cursor.off_y;

    // make sure there is a line under the cursor
    // needed when the buffer is empty
    if(line_idx == v->buff->lines.len) {
        buffer_line_insert(v->buff, line_idx, line_new());
    }

    struct Line *l = buffer_line_get(v->buff, v->view_cursor.off_y);

    // safe end of line if the cursor is not at the end of that line
    char *end_of_line = 0;
    size_t end_of_line_len = 0;
    if(v->view_cursor.off_x < str_len(&l->text)) {
        Str tail = str_tail(&l->text, v->view_cursor.off_x);
        end_of_line_len = str_cstr_len(&tail);
        end_of_line = xcalloc(end_of_line_len, sizeof(char));
        memcpy(end_of_line, str_as_cstr(&tail), end_of_line_len);
        line_trunc(l, v->view_cursor.off_x);
    }

    size_t last_off = 0;
    size_t off = 0;
    while(off < len) {
        while(off < len && s[off] != '\n' && s[off] != '\0') off += 1;
        line_append(l, s + last_off, off - last_off);
        v->view_cursor.off_x = str_len(&l->text);
        if(s[off] == '\n') {
            off += 1;
            buffer_line_insert(v->buff, v->view_cursor.off_y+1, line_new());
            v->view_cursor.off_y += 1;
            // select the new line
            l = buffer_line_get(v->buff, v->view_cursor.off_y);
            // move cursor at the end of that line
            v->view_cursor.off_x = str_len(&l->text);
        }
        last_off = off;
    }

    // append the saved end of line if there is one
    if(end_of_line) {
        line_append(l, end_of_line, end_of_line_len);
        free(end_of_line);
    }

    // rerun the search
    if(v->buff->re_state.matches.len) {
        re_state_clear_matches(&v->buff->re_state);
        view_search_re(v);
    }

    return 0;
}

void view_clear_highlight(struct View *v) {
    for(size_t i = 0; i < v->buff->lines.len; i++) {
        vec_clear(&buffer_line_get(v->buff, i)->style_ids);
    }
}

void view_set_cursor(struct View *v, size_t x, size_t y) {
    if(!v->buff || !v->buff->lines.len) return;
    v->view_cursor.off_y = y < v->buff->lines.len ? y : v->buff->lines.len-1;
    if(v->view_cursor.off_y < v->line_off) {
        v->line_off = v->view_cursor.off_y;
        v->first_line_char_off = 0;
    }
    struct Line *l = buffer_line_get(v->buff, v->view_cursor.off_y);

    v->view_cursor.off_x = x < str_len(&l->text) ? x : str_len(&l->text);
    if(v->view_cursor.off_y == v->line_off && v->first_line_char_off > v->view_cursor.off_x) {
        v->first_line_char_off = 0;
    }
}

utf32 view_get_cursor_char(const struct View *v) {
    struct Line *l = buffer_line_get(v->buff, v->view_cursor.off_y);
    if(v->view_cursor.off_x == str_len(&l->text)) {
        return L'\n';
    }
    utf32 c = 0;
    str_get_char(&l->text, v->view_cursor.off_x, &c);
    return c;
}

struct ViewCursor view_get_cursor(const struct View *v) {
    return v->view_cursor;
}

struct ViewSelection view_selection_empty(void) {
    return (struct ViewSelection) {
        .start = {
            .off_x = SIZE_MAX,
            .off_y = SIZE_MAX,
        },
        .end = {
            .off_x = SIZE_MAX,
            .off_y = SIZE_MAX,
        },
    };
}

// Returns a view_selection where the start is guaranteed to come before the end
struct ViewSelection view_selection_from_cursors(struct ViewCursor a, struct ViewCursor b) {
    ViewCursor start = a;
    ViewCursor end = b;


    _Bool end_comes_before_start =
        b.off_y < a.off_y
        || (b.off_y == a.off_y
            && b.off_x < a.off_x)
        ;

    if(end_comes_before_start) {
        end = a;
        start = b;
    }
    return (struct ViewSelection) {
        .start = start,
        .end = end,
    };
}

int view_selection_position_selected(const struct ViewSelection *vs, size_t line_idx, size_t char_off) {

    int line_fully_selected = vs->start.off_y < line_idx && vs->end.off_y > line_idx;

    int tail_selected = vs->start.off_y == line_idx
        && char_off >= vs->start.off_x
        && vs->end.off_y != line_idx;

    int head_selected = vs->end.off_y == line_idx 
        && char_off <= vs->end.off_x
        && vs->start.off_y != line_idx;

    int middle_selected = vs->start.off_y == line_idx
        && vs->end.off_y == line_idx
        && vs->start.off_x <= char_off
        && vs->end.off_x >= char_off;

    return line_fully_selected || head_selected || middle_selected || tail_selected;
}

Str view_selection_get_text(const struct ViewSelection *vs, const struct View *v) {
    Str selected = str_new();

    for(size_t line_idx = vs->start.off_y;
            line_idx <= vs->end.off_y;
            line_idx++) {

        struct Line *l = buffer_line_get(v->buff, line_idx);
        for(size_t char_off = 0;
                char_off < str_len(&l->text);
                char_off++) {
            if(view_selection_position_selected(vs, line_idx, char_off)) {
                size_t char_idx = str_get_char_byte_idx(&l->text, char_off);
                size_t char_len = str_get_char_byte_idx(&l->text, char_off+1) - char_idx;

                str_push(&selected, str_as_cstr(&l->text) + char_idx, char_len);
            }
        }
        if(line_idx != vs->end.off_y || (line_idx == vs->end.off_y && vs->end.off_x == str_len(&l->text)))
            str_push(&selected, "\n", 2);
    }

    return selected;
}

void view_move_cursor_start(struct View *v) {
    view_set_cursor(v, 0, v->view_cursor.off_y);
}

void view_move_cursor_end(struct View *v) {
    view_set_cursor(v, SIZE_MAX, v->view_cursor.off_y);
}

void view_move_cursor(struct View *v, ssize_t off_x, ssize_t off_y) {
    ssize_t new_x;
    new_x = (ssize_t)v->view_cursor.off_x + off_x;        
    if(new_x < 0) new_x = 0;

    if(off_x) {
        v->view_cursor.target_x = new_x;
    } else if(new_x < (ssize_t)v->view_cursor.target_x) {
        new_x = v->view_cursor.target_x;
    }

    ssize_t new_y = (ssize_t)v->view_cursor.off_y + off_y;
    if(new_y < 0) new_y = 0;

    view_set_cursor(v, new_x, new_y);
}
/*
static int render_plan_render(const struct View *restrict v, struct AbsoluteCursor *ac) {
    const struct RenderPlan *rp = &v->rp;
    Style base_style = v->style;
    Style line_num_style = style_fg(base_style, colour_vt(VT_GRA));
    Style highlight = style_bg(base_style, colour_vt(VT_BLU));

    struct ViewSelection vs = view_selection_empty();

    match_maybe(&v->selection_end,
        end, {
            vs = view_selection_from_cursors(
                v->view_cursor,
                *end);
            },
        {}
    );

    // render full lines
    size_t count;
    int spill = 0;
    for(count = 0; count < rp->fully_rendered_lines; count++) {
        struct Line l = *buffer_line_get(v->buff, count + v->line_off);

        if(!count) {
            line_tail(&l, rp->first_line_char_off);
        }

        // print first "real" line
        set_cursor_pos(rp->vp->off_x, rp->vp->off_y+count+spill);
        // print line number
        if(rp->num_width > 1) {
            style_fmt(
                    &line_num_style,
                    STDOUT_FILENO,
                    "%*ld ",
                    rp->num_width -1,
                    count + v->line_off + 1
                );
        }

        size_t take_width = rp->line_max_width;
        ssize_t len = take_cols(&l.text, &take_width, CONFIG.tab_width);
        if(len == -1) return -1;

        size_t char_offset = 0;

        if(view_selection_line_fully_selected(&vs, count + v->line_off)) {
            if(write_escaped(&highlight, &l, char_offset, len) == -1) return -1;
            if(take_width < rp->line_max_width) {
                if(write_char_escaped(&highlight, L' ', STDOUT_FILENO) == -1) return -1;
            }
        } else if(view_selection_line_tail_partially_selected(&vs, char_offset, count + v->line_off)) {
            size_t i = 0;
            for(i = 0;i < (size_t)len; i++) {
                if(view_selection_position_selected(&vs, char_offset+i, count+v->line_off)) {
                    if(write_escaped(&highlight, &l, char_offset+i, 1) == -1) return -1;
                } else {
                    if(write_escaped(&base_style, &l, char_offset+i, 1) == -1) return -1;
                }
            }
            // draw trailing \n if the selection extend to it and if there is space to draw it
            if(view_selection_position_selected(&vs, char_offset+i, count+v->line_off)) {
                if(take_width < rp->line_max_width) {
                    if(write_char_escaped(&highlight, L' ', STDOUT_FILENO) == -1) return -1;
                }
            }

        } else {
            // not selected at all;
            if(write_escaped(&base_style, &l, char_offset, len) == -1) return -1;
        }
        char_offset += len;

        size_t fill = rp->line_max_width - take_width;
        if(fill) {
            style_fmt(&base_style, STDOUT_FILENO, "%*c", fill, ' ');
        }

        // print the wraparound lines
        while(char_offset < str_len(&l.text)) {
            spill += 1;
            set_cursor_pos(rp->vp->off_x, rp->vp->off_y+count+spill);
            if(rp->num_width) {
                style_fmt(
                        &line_num_style,
                        STDOUT_FILENO,
                        "%*c",
                        rp->num_width,
                        ' '
                    );
            }

            take_width = rp->line_max_width;
            Str rest = str_tail(&l.text, char_offset);
            ssize_t len = take_cols(&rest, &take_width, CONFIG.tab_width);
            if(len == -1) return -1;

            if(view_selection_line_fully_selected(&vs, count + v->line_off)) {
                if(write_escaped(&highlight, &l, char_offset, len) == -1) return -1;
            } else if(view_selection_line_tail_partially_selected(&vs, char_offset, count + v->line_off)) {
                for(size_t i = 0;i < (size_t)len; i++) {
                    if(view_selection_position_selected(&vs, char_offset+i, count+v->line_off)) {
                        if(write_escaped(&highlight, &l, char_offset+i, 1) == -1) return -1;
                    } else {
                        if(write_escaped(&base_style, &l, char_offset+i, 1) == -1) return -1;
                    }
                }
            } else {
                // not selected at all;
                if(write_escaped(&base_style, &l, char_offset, len) == -1) return -1;
            }
            char_offset += len;
            size_t fill = rp->line_max_width - take_width;
            if(fill) {
                style_fmt(&base_style, STDOUT_FILENO, "%*c", fill, ' ');
            }
        }

    }

    if(rp->last_line_chars != SIZE_MAX) {
        struct Line *l = buffer_line_get(v->buff, count + v->line_off);
        // print first "real" line
        set_cursor_pos(rp->vp->off_x, rp->vp->off_y+count+spill);
        // print line number
        if(rp->num_width > 1) {
            style_fmt(
                    &line_num_style,
                    STDOUT_FILENO,
                    "%*ld ",
                    rp->num_width -1,
                    count + v->line_off + 1
                );
        }


        if(write_escaped(&base_style, l, 0, rp->last_line_chars) == -1) return -1;

        Str head = str_head(&l->text, rp->last_line_chars+1);
        size_t width = render_width(&head, str_len(&head));
        size_t fill = rp->line_max_width - width;
        if(fill) {
            style_fmt(&base_style, STDOUT_FILENO, "%*c", fill, ' ');
        }
        count += 1;
    }

    for(size_t i = count+spill; i < rp->real_height; i++) {
        set_cursor_pos(rp->vp->off_x, rp->vp->off_y+i);
        if(rp->num_width > 1) {
            style_fmt(
                    &line_num_style,
                    STDOUT_FILENO,
                    "%*c ",
                    rp->num_width -2,
                    '~'
                );
        }
        style_fmt(&base_style, STDOUT_FILENO, "%*c", rp->line_max_width, ' ');

    }

    if(ac) {
        *ac = rp->cursor;
    }

    return 0;
}
*/

uint16_t view_num_width(const struct View *v) {
    if(v->options.no_line_num) return 0;
    // log10 100 = 2, so +1 for 100 and another +1
    // for a space between the number and the text
    return log10(v->buff->lines.len) + 2;
}

uint16_t view_inner_width(
        const struct View *v,
        const ViewPort * vp) {
    uint16_t width = vp->width;

    uint16_t num_width = view_num_width(v);
    if(num_width > width) return 0;
    width -= num_width;

    return width;
}

int view_render(struct View *v, ViewPort *vp, const struct winsize *ws, struct AbsoluteCursor *ac) {
    // TODO(louis) use winsize to cutoff text that is partially out of the screen

    Style base_style = v->style;
    Style line_num_style = style_fg(base_style, colour_vt(VT_GRA));
    Style highlight = style_bg(base_style, colour_vt(VT_BLU));

    uint16_t width = view_inner_width(v, vp);

    uint16_t height = vp->height;

    // check if the cursor is beyond the end of the screen
    {
        uint16_t leading_height = 0;
        size_t first_line_char_off = 0;
        ssize_t line_off = v->view_cursor.off_y;
        while(line_off >= (ssize_t)v->line_off && leading_height <= height) {
            struct Line l = *buffer_line_get(v->buff, line_off);
            if(line_off == (ssize_t)v->view_cursor.off_y) {
                l = line_head(&l, v->view_cursor.off_x+1);
            }

            if(l.render_width > width) {
                leading_height += l.render_width / width;
                leading_height += (l.render_width % width != 0);
            } else {
                leading_height += 1;
            }

            if(leading_height > height) {
                size_t diff = leading_height - height;
                size_t col_off = width * diff;
                first_line_char_off = take_cols(&l.text, &col_off, CONFIG.tab_width);
                if(first_line_char_off >= str_len(&l.text)) {
                    first_line_char_off = 0;
                    line_off++;
                }
                break;
            }
            line_off--;
        }
        if(line_off < 0) line_off = 0;
        if(line_off > (ssize_t)v->line_off) {
            v->first_line_char_off = first_line_char_off;
            v->line_off = line_off;
        } else if(first_line_char_off > v->first_line_char_off) {
            v->first_line_char_off = first_line_char_off;
        }
    }

    // render text
    size_t text_height = 0;
    size_t line_idx = 0;
    while(text_height < height) {
        struct Line *ptr = buffer_line_get(
                v->buff,
                v->line_off + line_idx);
        if(!ptr) break;
        struct Line l = *ptr;
        char *fmt_str = "%*ld ";

        if(v->first_line_char_off && line_idx == 0) {
            l = line_tail(&l, v->first_line_char_off);
            fmt_str = "%*ld^";
        }
        // TODO(louis) highlight
        set_cursor_pos(vp->off_x, vp->off_y+text_height);
        // print line number
        if(view_num_width(v) > 0) {
            style_fmt(
                    &line_num_style,
                    STDOUT_FILENO,
                    fmt_str,
                    view_num_width(v) -1,
                    line_idx + v->line_off + 1
                );
        }

        size_t target_width = width;
        ssize_t idx = take_cols(
                &l.text,
                &target_width,
                CONFIG.tab_width);
        if(idx == -1) return -1;

        struct ViewSelection vs = view_selection_empty();

        match_maybe(&v->selection_end,
            end, {
                vs = view_selection_from_cursors(
                    v->view_cursor,
                    *end);
                },
            {}
        );

        if(view_write_escaped(
                    &v->style,
                    v,
                    line_idx + v->line_off,
                    line_idx == 0 ? v->first_line_char_off : 0,
                    idx,
                    &highlight,
                    &vs) < 0) assert(0);

        if(ac && line_idx + v->line_off == v->view_cursor.off_y) {
            size_t line_cursor_pos = v->view_cursor.off_x;
            if(v->view_cursor.off_y == v->line_off) {
                line_cursor_pos += v->first_line_char_off;
            }
            size_t line_width = render_width(&l.text, line_cursor_pos);
            ac->row = vp->off_y
                + text_height
                + line_width / width;
            ac->col = vp->off_x
                + view_num_width(v)
                + line_width % width;
        }

        size_t fill = width - target_width;
        if(fill) {
            style_fmt(&base_style, STDOUT_FILENO, "%*c", fill, ' ');
        }

        size_t line_char_off = idx + (line_idx == 0 ? v->first_line_char_off : 0);
        l = line_tail(&l, idx);
        text_height += 1;
        // print the wraparound lines
        while(str_len(&l.text) && text_height < height) {
            set_cursor_pos(vp->off_x, vp->off_y+text_height);
            // print line number
            if(view_num_width(v) > 0) {
                style_fmt(
                        &line_num_style,
                        STDOUT_FILENO,
                        "%*c",
                        view_num_width(v),
                        ' '
                    );
            }
            size_t target_width = width;
            size_t idx = take_cols(
                    &l.text,
                    &target_width,
                    CONFIG.tab_width);
            if(idx == SIZE_MAX) assert(0);

            if(view_write_escaped(&v->style, v, line_idx + v->line_off, line_char_off, idx, &highlight, &vs) < 0) assert(0);

            size_t fill = width - target_width;
            if(fill) {
                style_fmt(&base_style, STDOUT_FILENO, "%*c", fill, ' ');
            }
            line_char_off += idx;
            l = line_tail(&l, idx);
            text_height += 1;
        }
        line_idx += 1;
    }

    // render empty lines
    while(text_height < height) {
        set_cursor_pos(vp->off_x, vp->off_y+text_height);
        if(view_num_width(v) > 0) {
            style_fmt(
                    &line_num_style,
                    STDOUT_FILENO,
                    "%*c",
                    view_num_width(v)-1,
                    '~'
                );
        }
        style_fmt(&base_style, STDOUT_FILENO, "%*c", vp->width, ' ');
        text_height += 1;
    }
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
        xfree(w->child);
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

int tabs_last(void) {
    if(TABS.len) {
        ACTIVE_TAB = TABS.len-1;
    }
    return ACTIVE_TAB;
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
                int idx = take_cols_rev(&tab_get(i)->name, &cols, CONFIG.tab_width);
                assert(idx >= 0 && "error when computing idx");
                Str name_str = tab_get(i)->name;
                const char *name = str_as_cstr(&name_str) + idx;
                dprintf(STDOUT_FILENO, "[%s]", name);
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
    buffer_rc_inc(v->buff);
    return view;
}

int view_erase(struct View *v) {
    size_t cursor = v->view_cursor.off_x;

    match_maybe(&v->selection_end,
        selection_end, {
            struct ViewSelection vs = view_selection_from_cursors(v->view_cursor, *selection_end);

            struct Line *last_line = buffer_line_get(v->buff, vs.end.off_y);
            Str last_line_tail = str_new();
            // tries to get the end of the line, if end+1 is included ie: newline
            // copy line end.off_y +1 instead, and then delete it
            if(vs.end.off_x < str_len(&last_line->text)) {
                last_line_tail = str_tail(&last_line->text, vs.end.off_x+1);

                // clone the line, this might be needed if the
                // first line and the last line are the same line
                last_line_tail = str_clone(&last_line_tail);
            } else if(vs.end.off_y+1 < v->buff->lines.len) {
                struct Line *past_last_line = buffer_line_get(v->buff, vs.end.off_y+1);
                last_line_tail = str_clone(&past_last_line->text);
                buffer_line_remove(v->buff, vs.end.off_y+1);
            }

            struct Line *first_line = buffer_line_get(v->buff, vs.start.off_y);
            // trunk first line
            line_trunc(first_line, vs.start.off_x);
            // append the end of the last line
            line_append(first_line, str_as_cstr(&last_line_tail), str_cstr_len(&last_line_tail));
            str_free(&last_line_tail);

            size_t selected_lines_count = vs.end.off_y - vs.start.off_y + 1;
            for(size_t i = 1; i < selected_lines_count; i++) {
                buffer_line_remove(v->buff, vs.start.off_y + 1);
            }
        },
        {
            struct Line *line = buffer_line_get(v->buff, v->view_cursor.off_y);
            if(cursor > 0) {
                v->buff->dirty = 1;
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

                line_remove(line, start, cursor-1);
                v->view_cursor.off_x -= cursor - start;
            } else if(v->view_cursor.off_y > 0) {
                v->buff->dirty = 1;
                struct Line *prev_line = buffer_line_get(v->buff, v->view_cursor.off_y -1);
                // move cursor to end of previous line
                v->view_cursor.off_x = str_len(&prev_line->text);
                line_append(prev_line, str_as_cstr(&line->text), str_cstr_len(&line->text));

                buffer_line_remove(v->buff, v->view_cursor.off_y);
                v->view_cursor.off_y -= 1;
            }
        }
    );

    // rerun the search
    if(v->buff->re_state.matches.len) {
        re_state_clear_matches(&v->buff->re_state);
        view_search_re(v);
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

int iswordpunct(wint_t c) {
    return c != L'_' && iswpunct(c);
}
int isword(wint_t c) {
    return !iswordpunct(c) && !iswspace(c);
}

int view_move_cursor_word_start(struct View *v) {
    struct Line *l = buffer_line_get(v->buff, v->view_cursor.off_y);

    utf32 c = 0;
    // I miss closures
    // TODO(louis) move to it's own function
    #define WALK_BACK_CURSOR(test) \
        if(v->view_cursor.off_x == 0) { \
            /* test is used to check newlines against the current func */ \
            if(!(test) || v->view_cursor.off_y == 0) break; \
            view_move_cursor(v, 0, -1); \
            view_move_cursor_end(v); \
            l = buffer_line_get(v->buff, v->view_cursor.off_y); \
        } else { \
            view_move_cursor(v, -1, 0); \
        }
    // never break
    do { WALK_BACK_CURSOR(1); } while(0);
    if(str_get_char(&l->text, v->view_cursor.off_x, &c)) return -1;

    int (*func)(utf32) = isword;
    if(iswordpunct(c)) func = iswordpunct;
    else if(iswspace(c)) func = iswspace;

    while(!func(c)) {
        WALK_BACK_CURSOR(!func('\n'));
        if(str_get_char(&l->text, v->view_cursor.off_x, &c)) return -1;
    }

    struct ViewCursor last = v->view_cursor;
    while(func(c)) {
        last = v->view_cursor;
        WALK_BACK_CURSOR(func('\n'));
        if(str_get_char(&l->text, v->view_cursor.off_x, &c)) return -1;
    }
    // walk forward one position
    if(func != iswspace) v->view_cursor = last;
    return 0;
}

int view_move_cursor_word_end(struct View *v) {
    struct Line *l = buffer_line_get(v->buff, v->view_cursor.off_y);

    utf32 c = 0;
    // I miss closures
    #define ADVANCE_CURSOR(test) \
        if(v->view_cursor.off_x + 1 >= str_len(&l->text)) { \
            /* test is used to check newlines against the current func */ \
            if(!(test) || v->view_cursor.off_y + 1 >= v->buff->lines.len) break; \
            view_move_cursor(v, 0, 1); \
            view_move_cursor_start(v); \
            l = buffer_line_get(v->buff, v->view_cursor.off_y); \
        } else { \
            view_move_cursor(v, 1, 0); \
        }
    // never break
    do { ADVANCE_CURSOR(1); } while(0);
    if(str_get_char(&l->text, v->view_cursor.off_x, &c)) return -1;

    int (*func)(utf32) = isword;
    if(iswordpunct(c)) func = iswordpunct;
    else if(iswspace(c)) func = iswspace;

    while(!func(c)) {
        ADVANCE_CURSOR(!func('\n'));
        if(str_get_char(&l->text, v->view_cursor.off_x, &c)) return -1;
    }

    struct ViewCursor last = v->view_cursor;
    while(func(c)) {
        last = v->view_cursor;
        ADVANCE_CURSOR(func('\n'));
        if(str_get_char(&l->text, v->view_cursor.off_x, &c)) return -1;
    }
    #undef ADVANCE_CURSOR
    // walk back one position
    if(func != iswspace) v->view_cursor = last;
    return 0;
}

int view_move_cursor_word_next(struct View *v) {
    struct Line *l = buffer_line_get(v->buff, v->view_cursor.off_y);

    utf32 c = 0;
    // I miss closures
    #define ADVANCE_CURSOR(test) \
        if(v->view_cursor.off_x + 1 >= str_len(&l->text)) { \
            /* test is used to check newlines against the current func */ \
            if(!(test) || v->view_cursor.off_y + 1 >= v->buff->lines.len) break; \
            view_move_cursor(v, 0, 1); \
            view_move_cursor_start(v); \
            l = buffer_line_get(v->buff, v->view_cursor.off_y); \
        } else { \
            view_move_cursor(v, 1, 0); \
        }
    // only errors out if the line has a length of zero
    if(str_get_char(&l->text, v->view_cursor.off_x, &c) == -1) c = L'\n';

    int (*func)(utf32) = isword;
    if(iswordpunct(c)) func = iswordpunct;
    else if(iswspace(c)) func = iswspace;

    // never break
    do {
        if(v->view_cursor.off_x + 1 >= str_len(&l->text)) { \
            /* test is used to check newlines against the current func */ \
            if(!(1) || v->view_cursor.off_y + 1 >= v->buff->lines.len) break; \
            view_move_cursor(v, 0, 1); \
            view_move_cursor_start(v); \
            l = buffer_line_get(v->buff, v->view_cursor.off_y); \
        } else { \
            view_move_cursor(v, 1, 0); \
        }
    } while(0);
    if(str_get_char(&l->text, v->view_cursor.off_x, &c)) return -1;

    while(func(c)) {
        ADVANCE_CURSOR(func('\n'));
        if(str_get_char(&l->text, v->view_cursor.off_x, &c)) return -1;
    }

    while(iswspace(c)) {
        ADVANCE_CURSOR(iswspace('\n'));
        if(str_get_char(&l->text, v->view_cursor.off_x, &c)) return -1;
    }

    #undef ADVANCE_CURSOR
    return 0;
}

int normal_handle_key(struct KeyEvent *e) {
    struct View *v = tab_active_view(tab_active());

    if(e->modifier == 0) {
        switch(e->key) {
            case 'G': {
                view_move_cursor(v, 0, v->buff->lines.len);
            } break;
            case 'b': {
                if(view_move_cursor_word_start(v)) return -1;
            } break;
            case 'e': {
                if(view_move_cursor_word_end(v)) return -1;
            } break;
            case 'w': {
                if(view_move_cursor_word_next(v)) return -1;
            } break;
            case '^': {
                view_move_cursor_start(v);
                struct Line *l = buffer_line_get(v->buff, v->view_cursor.off_y);
                for(size_t i = 0; i < str_len(&l->text); i++) {
                    utf32 c = view_get_cursor_char(v);
                    wint_t wc = utf32_to_wint(c);
                    if(!iswspace(wc)) break;
                    view_move_cursor(v, +1, 0);
                }
            } break;
            case 'O': {
                view_move_cursor_start(v);
                view_write(v, "\n", sizeof("\n")-1);
                view_move_cursor(v, 0, -1);
                mode_change(M_Insert);
            } break;
            case 'A': {
                view_move_cursor_end(v);
                mode_change(M_Insert);
            } break;
            case 'o': {
                view_move_cursor_end(v);
                view_write(v, "\n", sizeof("\n")-1);
                mode_change(M_Insert);
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
            case 'a': {
                view_move_cursor(v, +1,0);
                mode_change(M_Insert);
            } break;
            case ':': {
                mode_change(M_Command);
            } break;
            case '/': {
                mode_change(M_Search);
            } break;
            case 'p': {
                Str selection = str_new();
                if(clipboard_get(&selection)) {
                    message_print("E: failed to paste: '%s'", strerror(errno));
                } else {
                    view_write(v, str_as_cstr(&selection), str_cstr_len(&selection));
                }
                str_free(&selection);
            } break;
            case 'n': {
                cursor_jump_next_search();
            } break;
            case 'N': {
                cursor_jump_prev_search();
            } break;
            default:
                break;
        }
    } else if (e->modifier == KM_Ctrl) {
        switch(e->key) {
            case 'e': {
                if(v->line_off < v->buff->lines.len-1) {
                    if(v->view_cursor.off_y == v->line_off) {
                        view_move_cursor(v, 0, +1);
                    }
                    v->line_off += 1;
                }
            } break;
            case 'y': {
                if(v->line_off > 0) {
                    /*
                    if(v->view_cursor.off_y == render_plan_line_count(&v->vp) + v->line_off -1) {
                        view_move_cursor(v, 0, -1);
                    }
                    */
                    v->line_off -= 1;
                    v->first_line_char_off = 0;
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
    fsync(STDOUT_FILENO);
    return 0;
}

int insert_leave(void) {
    char block_cursor[] = CSI"1 q";
    write(STDOUT_FILENO, block_cursor, sizeof(block_cursor)-1);
    fsync(STDOUT_FILENO);
    // might be run during shutdown
    if(TABS.len == 0) return 0;
    struct View *v = tab_active_view(tab_active());
    view_move_cursor(v, -1, 0);
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
    } else if(e->key == KC_DEL) {
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
    if(e->modifier == 0) {
        if(e->key == '\e') {
            mode_change(M_Normal);
            return 0;
        } else if(e->key == KC_DEL) {
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
        case KC_ARRLEFT:
            break;
        default: {
            // try not to print garbage to the screen
            if(!iswprint(e->key)) break;
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
    match_maybe(&v->selection_end,
        selection_end, {
            view_set_cursor(v, selection_end->off_x, selection_end->off_y);
            set_none(&v->selection_end);
        },
        {}
    );
    return 0;
}

int visual_handle_key(struct KeyEvent *e) {
    struct View *v = tab_active_view(tab_active());

    if(e->modifier == 0) {
        switch(e->key) {
            case 'G': {
                view_move_cursor(v, 0, v->buff->lines.len);
            } break;
            case 'b': {
                if(view_move_cursor_word_start(v)) return -1;
            } break;
            case 'e': {
                if(view_move_cursor_word_end(v)) return -1;
            } break;
            case 'w': {
                if(view_move_cursor_word_next(v)) return -1;
            } break;
            case '^': {
                view_move_cursor_start(v);
                struct Line *l = buffer_line_get(v->buff, v->view_cursor.off_y);
                for(size_t i = 0; i < str_len(&l->text); i++) {
                    utf32 c = view_get_cursor_char(v);
                    wint_t wc = utf32_to_wint(c);
                    if(!iswspace(wc)) break;
                    view_move_cursor(v, +1, 0);
                }
            } break;
            case '$': {
                view_move_cursor_end(v);
            } break;
            case '0': {
                view_move_cursor_start(v);
            } break;
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
            case 'd': {
                view_erase(v);
                struct ViewCursor a = v->view_cursor;
                struct ViewCursor b = *as_ptr(&v->selection_end);
                // if selection end comes before the current cursor
                // move the cursor to selection end
                if(a.off_y > b.off_y) {
                    v->view_cursor = b;
                } else if (a.off_x > b.off_x) {
                    v->view_cursor = b;
                }
                // this skips the mode change setting the position
                set_none(&v->selection_end);
                mode_change(M_Normal);
            } break;
            case 'y': {
                struct ViewSelection vs = view_selection_from_cursors(
                    *as_ptr(&v->selection_end),
                    v->view_cursor
                );
                Str selected = view_selection_get_text(&vs, v);
                if(clipboard_set(str_as_cstr(&selected), str_cstr_len(&selected))) {
                    message_print("E: failed to copy: '%s'", strerror(errno));
                } else {
                    match_maybe(&v->selection_end,
                            selection_end, {
                                view_set_cursor(v, selection_end->off_x, selection_end->off_y);
                            },
                            {}
                        );
                    mode_change(M_Normal);
                }
                str_free(&selected);
            } break;
        }
    }
    return 0;
}

int search_enter(void) {
    struct View *active_view = tab_active_view(tab_active());
    active_view->buff->re_state.original_cursor = active_view->view_cursor;

    message_print("/");
    insert_enter();
    return 0;
}

int search_leave(void) {
    struct View *active_view = tab_active_view(tab_active());
    if(active_view->buff->re_state.error_str) {
        message_print(
            "E: regex error: '%s'",
            active_view->buff->re_state.error_str);
    }
    insert_leave();
    return 0;
}

int search_handle_key(struct KeyEvent *e) {
    // TODO(louis) handle command buffer here
    if(e->modifier == 0) {
        if(e->key == '\e') {
            struct View *v = tab_active_view(tab_active());
            view_set_cursor(
                    v,
                    v->buff->re_state.original_cursor.off_x,
                    v->buff->re_state.original_cursor.off_y);
            mode_change(M_Normal);
            return 0;
        } else if(e->key == KC_DEL) {
            // do not erase the leading ':'
            if(MESSAGE.view_cursor.off_x > 1) {
                view_erase(&MESSAGE);
            }
            return 0;
        } else if(e->key == '\n') {
            mode_change(M_Normal);
            return 0;
        }
    }
    switch(e->key) {
        default: {
            char bytes[4] = {0};
            int len = utf32_to_utf8(e->key, bytes, 4);
            assert(len >= 1);
            message_append("%.*s", len, bytes);
            struct Line *line = buffer_line_get(MESSAGE.buff, 0);
            // this only checks the first line
            editor_search(str_as_cstr(&line->text)+1);
            struct View *active_view = tab_active_view(tab_active());
            active_view->view_cursor = active_view->buff->re_state.original_cursor;
            cursor_jump_next_search();
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
    (struct ModeInterface){
        .mode_str = "SEA",
        .handle_key = search_handle_key,
        .on_enter = search_enter,
        .on_leave = search_leave,
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
    size_t msg_line_height = MESSAGE.buff->lines.len;
    for(size_t i = 0; i < MESSAGE.buff->lines.len; i++) {
        struct Line *message_line = buffer_line_get(MESSAGE.buff, i);
        msg_line_height += message_line->render_width / ws->ws_col;
    }
    // always render at least 1 line
    if(msg_line_height == 0) return 1;
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

    set_cursor_pos(0, ws->ws_row - 1 - message_line_render_height(ws));
    style_fmt(
            &active_line_style,
            STDOUT_FILENO,
            "[%s] (%ld, %ld) %ld/%ld %ld %s",
            mode_current().mode_str,
            v->view_cursor.off_x + 1,
            v->view_cursor.off_y + 1,
            v->line_off + 1,
            v->buff->lines.len,
            v->first_line_char_off,
            FILEMODE_REPR[v->buff->fm]
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

void editor_quit_all() {
    RUNNING = 0;
}

void editor_quit(int no_confirm) {
    struct Tab *active_tab = tab_active();

    struct Window *active_window = tab_window_active(active_tab);
    struct View *v = tab_active_view(tab_active());

    if(!no_confirm && v->buff->rc == 0 && v->buff->dirty && v->buff->in.ty != INPUT_SCRATCH) {
        message_print("E: modifications to this buffer would be lost");
        return;
    }

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
    struct Buffer *buff = xcalloc(1, sizeof(struct Buffer));

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
    // switch to last tab
    tabs_last();
}

void window_push(struct Window *win, struct Window *sub_win, enum SplitDir split) {
    if(win->child) {
        sub_win->child = win->child;
    }

    win->child = sub_win;
    win->split_dir = split;
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

    window_push(active_window, new_win, split);

    // switch focus to new window
    active_tab->active_window += 1;

    return;
}

void editor_open(const char *path, enum FileMode fm, int no_confirm) {
    struct Tab *active_tab = tab_active();

    struct Window *active_window = tab_window_active(active_tab);
    struct View *active_view = window_view_active(active_window);

    if(!no_confirm && active_view->buff->rc == 0 && active_view->buff->dirty && active_view->buff->in.ty != INPUT_SCRATCH) {
        message_print("E: modifications to this buffer would be lost");
        return;
    }

    if(!path) {
        switch(active_view->buff->in.ty) {
            case INPUT_SCRATCH:
                return;
            case INPUT_FILE:
                path = str_as_cstr(&active_view->buff->in.u.file.path);
            break;
        }
    }

    struct View new_view = {0};

    if(view_from_path(path, fm, &new_view)) {
        return;
    }

    if(active_tab->active_window == 0 && strcmp(str_as_cstr(&active_tab->name), path)) {
        size_t path_len = strlen(path);
        str_clear(&active_tab->name);
        str_push(&active_tab->name, path, path_len);
    }

    view_free(active_view);
    *active_view = new_view;
}

int clipboard_set(const char *s, size_t len) {

    SpawnHandle handle = {0};

    int res = spawn_captured(CONFIG.copy_command, &handle);
    if(res) return res;


    write(handle.stdin_fd, s, len);
    close(handle.stdin_fd);

    Str output = str_new();

    res = spawn_handle_wait_collect_output(&handle, &output);

    if(res || !str_is_empty(&output)) {
        message_print("%.*s", str_cstr_len(&output), str_as_cstr(&output));
        str_free(&output);
        return res;
    }
    str_free(&output);

    return 0;
}

int clipboard_get(Str *s) {
    SpawnHandle handle = {0};

    int res = spawn_captured(CONFIG.paste_command, &handle);
    if(res) return res;

    res = spawn_handle_wait_collect_output(&handle, s);

    if(res && !str_is_empty(s)) {
        message_print("%.*s", str_cstr_len(s), str_as_cstr(s));
        str_clear(s);
        return res;
    }

    return 0;
}

void editor_init(void) {
    MESSAGE.buff = xcalloc(1, sizeof(struct Buffer));
    *MESSAGE.buff = buffer_new();
    MESSAGE.options.no_line_num = 1;

    if(TABS.len == 0) {
        struct Buffer *buff = xcalloc(1, sizeof(struct Buffer));
        *buff = buffer_new();
        struct View view = view_new(buff);
        struct Window win = window_new();
        window_view_push(&win, view);
        struct Tab tab = tab_new(win, 0);
        tabs_push(tab);
    }

    style_register(
        SEARCH_HIGHLIGHT,
        sizeof(SEARCH_HIGHLIGHT) -1,
        style_bg(style_new(), colour_vt(VT_BLU)),
        1);
}

void cursor_jump_prev_search(void) {
    // TODO(louis) binary search
    struct View *active_view = tab_active_view(tab_active());
    if(!active_view->buff->re_state.matches.len) return;

    struct ReMatch *match = 0;
    size_t idx = active_view->buff->re_state.matches.len-1;

    match = vec_get(&active_view->buff->re_state.matches, idx);
    while(match->line > active_view->view_cursor.off_y
            || (match->line == active_view->view_cursor.off_y
                && match->col >= active_view->view_cursor.off_x)) {
        if(idx == 0) {
            match = vec_get(
                    &active_view->buff->re_state.matches,
                    active_view->buff->re_state.matches.len-1);
            break;
        }
        idx--;
        match = vec_get(&active_view->buff->re_state.matches, idx);
    }

    view_set_cursor(active_view, match->col, match->line);
}

void cursor_jump_next_search(void) {
    struct View *active_view = tab_active_view(tab_active());
    if(!active_view->buff->re_state.matches.len) return;

    struct ReMatch *match = 0;
    size_t idx = 0;

    match = vec_get(&active_view->buff->re_state.matches, idx);
    while(match->line < active_view->view_cursor.off_y
            || (match->line == active_view->view_cursor.off_y
                && match->col <= active_view->view_cursor.off_x)) {
        if(idx == active_view->buff->re_state.matches.len-1) {
            match = vec_get(&active_view->buff->re_state.matches, 0);
            break;
        }
        idx = (idx + 1) % active_view->buff->re_state.matches.len;
        match = vec_get(&active_view->buff->re_state.matches, idx);
    }

    view_set_cursor(active_view, match->col, match->line);
}

void view_search_re(struct View *v) {
    int ret;
    size_t matches_size = 50;
    regmatch_t *matches = xcalloc(matches_size, sizeof(regmatch_t));

    view_clear_highlight(v);

    Maybe(size_t) selected = None();

    for(size_t i = 0; i < v->buff->lines.len; i++) {
        size_t line_idx = (i + v->buff->re_state.original_cursor.off_y) % v->buff->lines.len;

        struct Line *l = buffer_line_get(v->buff, line_idx);

        // TODO(louis) maybe use REG_STARTED
        ret = regexec(v->buff->re_state.regex, str_as_cstr(&l->text), matches_size, matches, 0);
        if(ret == REG_NOMATCH) continue;

        if(l->style_ids.len < str_len(&l->text)) {
            size_t diff = str_len(&l->text) - l->style_ids.len;

            char zero = 0;
            for(size_t i = 0; i < diff; i++) {
                vec_push(&l->style_ids, &zero);
            }
        }

        for(size_t j = 0; j < matches_size; j++) {
            // this api sucks so bad
            if(matches[j].rm_eo == -1) {
                break;
            }

            struct ReMatch match = {
                .line = line_idx,
                .col = matches[j].rm_so,
                .len = matches[j].rm_eo - matches[j].rm_so,
            };

            if(is_none(&selected)
                    && match.line >= v->buff->re_state.original_cursor.off_y
                    && match.col >= v->buff->re_state.original_cursor.off_x
                    && v->buff->re_state.matches.len > 0) {

                set_some(&selected, v->buff->re_state.matches.len);
            }

            // TODO(louis) add search highlight here
            char search_hi_id = style_find_id(SEARCH_HIGHLIGHT);
            memset(l->style_ids.buf + match.col, search_hi_id, match.len);

            vec_push(&v->buff->re_state.matches, &match);
        }
    }
    free(matches);
}

void editor_search(const char *re_str) {
    struct View *active_view = tab_active_view(tab_active());
    int ret;

    re_state_reset(&active_view->buff->re_state);
    // Extended regexes break when a partial ( is present
    ret = regcomp(active_view->buff->re_state.regex, re_str, 0);

    if(ret) {
        active_view->buff->re_state.error_str = xcalloc(128, sizeof(char));
        regerror(
            ret,
            active_view->buff->re_state.regex,
            active_view->buff->re_state.error_str,
            127);
        return;
    }

    view_search_re(active_view);
}

void editor_teardown(void) {
    vec_cleanup(&TABS);
    view_free(&MESSAGE);
}

#ifdef TESTING

#include "tests.h"

TESTS_START

TEST_DEF(test_hello)
    ASSERT(1);
TEST_ENDDEF

TESTS_END

#endif
