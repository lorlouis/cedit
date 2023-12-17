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

size_t count_cols(const Str *line, int tab_width) {
    size_t sum = 0;
    size_t off = 0;
    while(off<str_len(line)) {
        utf32 c = 0;
        if(str_get_char(line, off, &c)) return -1;

        if(c == 0 || c == L'\n') break;

        int utf8_len = utf32_len_utf8(c);
        if(utf8_len == -1) {
            return -1;
        }

        int width = utf32_width(c);
        // -1 on non printable characters
        if(width <= 0) {
            off += 1;
            if(c == L'\t') sum += tab_width;
            continue;
        }

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
