#include "vt.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define _XOPEN_SOURCE 1
#include <wchar.h>

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

size_t count_cols(const char *restrict line, size_t line_len, int tab_width) {
    size_t sum = 0;
    size_t off = 0;
    while(off<line_len && *(line+off) && *(line+off) != '\n') {
        wchar_t c;
        int read = mbtowc(&c, line + off, line_len);
        // could be wide null (unlikely but still)
        if(!read) break;
        if(read == -1) {
            return -1;
        }

        int width = wcwidth(c);
        // -1 on non printable characters
        if(width <= 0) {
            off += read;
            if(c == L'\t') sum += tab_width;
            continue;
        }

        sum += width;
        off += read;
    }
    return sum;
}

ssize_t take_cols(const char *restrict line, size_t line_len, size_t *nb_cols, int tab_width) {
    size_t sum = 0;
    size_t off = 0;

    while(off<line_len && *(line+off) && *(line+off) != '\n') {
        wchar_t c;
        int read = mbtowc(&c, line + off, line_len);
        if(read == -1) {
            return -1;
        }

        // could be wide null (unlikely but still)
        if(!read) break;

        int width = wcwidth(c);

        // -1 on non printable characters
        if(width <= 0) {
            off += read;
            if(c == L'\t') sum += tab_width;
            continue;
        }

        if(sum + width > *nb_cols) {
            *nb_cols = sum;
            return off;
        }

        sum += width;
        off += read;
    }

    *nb_cols = sum;
    return off;
}
