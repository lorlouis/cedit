#include "vt.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#define _XOPEN_SOURCE 1
#include <wchar.h>

#define VT_MOD_BRIGHT 8


int style_begin(const Style *s, int fd) {
    if(!s) return 0;

    int ret = 0;
    int sum = 0;
    switch(s->fg.t) {
        case COL_NONE:
            break;
        case COL_VT: {
            ret = dprintf(fd, CSI"38;5;%dm", s->fg.vt);
            if(ret < 0) return ret;
            sum += ret;
        } break;
        case COL_RGB: {
            ret = dprintf(
                    fd,
                    CSI"38;2;%d;%d;%dm",
                    s->fg.rgb.r,
                    s->fg.rgb.g,
                    s->fg.rgb.b
                );
            if(ret < 0) return ret;
            sum += ret;
        } break;
    }
    switch(s->bg.t) {
        case COL_NONE:
            break;
        case COL_VT: {
            ret = dprintf(fd, CSI"48;5;%dm", s->bg.vt);
            if(ret < 0) return ret;
            sum += ret;
        } break;
        case COL_RGB: {
            ret = dprintf(
                    fd,
                    CSI"48;2;%d;%d;%dm",
                    s->bg.rgb.r,
                    s->bg.rgb.g,
                    s->bg.rgb.b
                );
            if(ret < 0) return ret;
            sum += ret;
        } break;
    }
    switch(s->weight) {
        case WEIGHT_NORMAL:
            break;
        case WEIGHT_BOLD: {
            ret = dprintf(fd, CSI"1m");
            if(ret < 0) return ret;
            sum += ret;
        } break;
        case WEIGHT_FAINT: {
            ret = dprintf(fd, CSI"2m");
            if(ret < 0) return ret;
            sum += ret;
        } break;
        break;
    }
    if(s->inverted) {
        ret = dprintf(fd, CSI"7m");
        if(ret < 0) return ret;
        sum += ret;
    }
    switch(s->underline) {
        case UNDERLINE_NONE:
            break;
        case UNDERLINE_SIMPLE: {
            ret = dprintf(fd, CSI"4m");
            if(ret < 0) return ret;
            sum += ret;
        } break;
        case UNDERLINE_DOUBLE: {
            ret = dprintf(fd, CSI"21m");
            if(ret < 0) return ret;
            sum += ret;
        } break;
        break;
    }

    return sum;
}

Colour colour_none() {
    return (Colour) {0};
}

Colour colour_vt(enum VtColour vt) {
    return (Colour) {
        .t = COL_VT,
        .vt = vt
    };
}

Colour colour_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (Colour) {
        .t = COL_RGB,
        .rgb = {
            .r = r,
            .g = g,
            .b = b,
        }
    };
}

Style style_new() {
    return (Style) {0};
}

Style style_fg(Style s, const Colour c) {
    s.fg = c;
    return s;
}

Style style_bg(Style s, const Colour c) {
    s.bg = c;
    return s;
}

Style style_underline_color(Style s, const Colour c) {
    s.underline_color = c;
    return s;
}

Style style_weight(Style s, enum FontWeight weight) {
    s.weight = weight;
    return s;
}

Style style_inverted(Style s, bool inverted) {
    s.inverted = inverted;
    return s;
}

Style style_underline(Style s, enum UnderlineStyle underline) {
    s.underline = underline;
    return s;
}

int style_reset(int fd) {
    return dprintf(fd, "%s", RESET);
}

int style_fmt(const Style *s, int fd, const char *fmt, ...) {
    va_list args;
    int ret;
    int count = 0;

    va_start(args, fmt);

    ret = style_begin(s, fd);
    if(ret < 0) return ret;
    count += ret;

    ret = vdprintf(fd, fmt, args);
    if(ret < 0) return ret;
    count += ret;

    va_end(args);

    ret = style_reset(fd);
    if(ret < 0) return ret;
    count += ret;

    return count;
}

// writes to stdout
int set_cursor_pos(uint16_t row, uint16_t col) {
    int ret = dprintf(STDOUT_FILENO, CSI "%d;%dH", col + 1, row + 1);
    if(ret < 0) return ret;
    return fsync(STDOUT_FILENO);
}

#define STRLEN(x) x, ((sizeof(x)/sizeof(char)) -1)

volatile int IN_ALTERNATE_BUF = 0;

void alternate_buf_enter(void) {
    if(IN_ALTERNATE_BUF) return;

    int ret = write(STDOUT_FILENO, STRLEN(BUF_ALT));
    if(ret != -1) {
        IN_ALTERNATE_BUF = 1;
    }
    return;
}

void alternate_buf_leave(void) {
    if(!IN_ALTERNATE_BUF) return;

    int ret = write(STDOUT_FILENO, STRLEN(BUF_MAIN));
    if(ret != -1) {
        IN_ALTERNATE_BUF = 0;
    }
    return;
}

size_t count_cols(const Str *line, int tab_width) {
    size_t sum = 0;
    size_t off = 0;
    int width = 0;
    while(off<str_len(line)) {
        utf32 c = 0;
        if(str_get_char(line, off, &c)) return -1;

        if(c == 0 || c == L'\n') break;

        int utf8_len = utf32_len_utf8(c);
        if(utf8_len == -1) {
            return -1;
        }

        width = utf32_width(c);

        sum += width;
        off += 1;
    }
    return sum;
}

ssize_t take_cols(const Str *restrict line, size_t *nb_cols, int tab_width) {
    size_t sum = 0;
    size_t off = 0;

    while(off<str_len(line)){
        utf32 c = 0;
        if(str_get_char(line, off, &c)) return -1;

        wint_t wc = utf32_to_wint(c);
        if(wc == 0 || wc == L'\n') break;

        int width = utf32_width(c);

        if(sum + width > *nb_cols) {
            *nb_cols = sum;
            return off;
        }

        sum += width;
        off += 1;
    }

    *nb_cols = sum;
    return off;
}
