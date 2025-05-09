#include "termkey.h"

#include <stdlib.h>
#include <wctype.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>

#include <assert.h>

#include "utf.h"

// Attempts to read an unsigned int from fd
// this reads one char past the end of the number
// to get that last char. To resolve ambiguity between
// `'\0'` and `EOF`, `INT_MAX` is returned when fd is empty;
//
// Returns
//  INT_MAX when stream is emptied (success)
//  0-255 (last char read) on success
//  -1 on error, check `errno`
//  -2 on invalid input
static int readuc(int fd, unsigned char *restrict i) {
    int ret = 0;
    unsigned char c = 0;
    while((ret = read(fd, &c, 1)) > 0 && isdigit(c)) {
        // check for overflow
        if(c > UCHAR_MAX / 10) return -2;
        *i *= 10;
        if(c > UCHAR_MAX - (c - '0')) return -2;
        *i += c - '0';
    }
    if(ret == -1) return -1;
    if(ret == 0) return INT_MAX;
    return c;
}

static int read_utf8(int fd, char (*out)[4], int count) {
    int ret;
    char c;
    for(int i = 1; i < count; i++) {
        ret = read(fd, &c, 1);
        assert(c != 0 && "null byte in stream");
        if(ret == -1) return -1;
        // missing a byte
        if(ret == 0) return -2;
        // the next byte is invalid
        if(!utf8_is_follow(c)) return -2;
        (*out)[i] = c;
    }
    return 0;
}

int readkey(int fd, struct KeyEvent *restrict e) {
    unsigned char c = 0;
    int ret = 0;

    ret = read(fd, &c, 1);
    if(ret == -1) return -1;
    if(ret == 0) return 0;

    // check for utf-8
    int utf8_len = utf8_byte_count(c);
    if(utf8_len > 1) {
        char s[4] = {c, 0, 0 , 0};
        if(read_utf8(fd, &s, utf8_len) < 0) return -1;
        if(-1 == utf8_to_utf32(s, sizeof(s), &e->key)) return -1;
        e->modifier = 0;
        return 1;
    } else if (utf8_len == 1 && isprint(c)) {
        e->key = c;
        e->modifier = 0;
        return 1;
    }

    // read second char
    ret = read(fd, &c, 1);
    if(ret == -1) return -1;

    // there was only one char
    if(!ret) {
        // handle C0 control codes
        switch(c) {
            case 0:
                // could also be <C-@>
                e->key = ' ';
                e->modifier = KM_Ctrl;
                break;
            case '\t':
                e->key = '\t';
                e->modifier = 0;
                break;
            case '\n':
                e->key = '\n';
                e->modifier = 0;
                break;
            case 1 ... 8:
            case 11 ... 26:
                // a..=z
                e->key = 'a' + c - 1;
                e->modifier = KM_Ctrl;
                break;
            case 27:
                // could also be <C-[>
                // could also be <C-3>
                // going for \e for compatibility reasons
                e->key = *ESC;
                e->modifier = 0;
                break;
            case 28:
                e->key = '4';
                e->modifier = KM_Ctrl;
                break;
            case 29:
                // could also be <C-]>
                e->key = '5';
                e->modifier = KM_Ctrl;
                break;
            case 30:
                // could also be <C-~>
                // could also be <C-^>
                e->key = '6';
                e->modifier = KM_Ctrl;
                break;
            case 31:
                // could also be <C-/>
                // could also be <C-_>
                // the same as <C-é> I think
                e->key = '7';
                e->modifier = KM_Ctrl;
                break;
            case 127:
                // could also be <C-?>
                // could also be <C-8>
                e->key = KC_DEL;
                e->modifier = 0;
                break;
            default:
                e->key = c;
                e->modifier = 0;
                break;
        }
        return 1;
    }
    // alt-char
    else if(c != '[') {
        e->key = c;
        e->modifier = KM_Alt;
        return 1;
    }

    // read third char
    c = 0;
    ret = read(fd, &c, 1);
    if(ret == -1) return -1;

    if(!ret) {
        e->key = '[';
        e->modifier = KM_Alt;
        return 1;
    }

    int key = 0;

    // handle basic letter sequence
    if(c>='A' && c <= 'Z') {
        switch(c) {
            case 'A':
                key = KC_ARRUP;
                break;
            case 'B':
                key = KC_ARRDOWN;
                break;
            case 'C':
                key = KC_ARRRIGHT;
                break;
            case 'D':
                key = KC_ARRLEFT;
                break;
            case 'F':
                key = KC_END;
                break;
            // treat keypad 5 like a normal 5
            case 'G':
                key = '5';
                break;
            case 'H':
                key = KC_HOME;
                break;
            default:
                key = c;
                break;
        }
    }

    unsigned char code = 0;
    if(isdigit(c)) {
        code = c - '0';
    }
    // this will read 1 char past the end of the number
    ret = readuc(fd, &code);
    if(ret == -1) return -1;
    else if (ret == INT_MAX) {
        e->key = key;
        e->modifier = 0;
        return 1;
    }
    // incomplete char code
    c = (unsigned char)ret;

    // check for mod+key
    if(code == 1 && c != '~') {
        switch(c) {
            case 'P':
                key = KC_F1;
                break;
            case 'Q':
                key = KC_F2;
                break;
            case 'R':
                key = KC_F3;
                break;
            case 'S':
                key = KC_F4;
                break;
            default:
                return -2;
        }
        // unget the last char
        ret = lseek(fd, -1, SEEK_CUR);
        if(ret == -1) return -1;
        e->key = key;
        e->modifier = 0;
        return 1;
    }
    switch(code) {
        case 1:
        case 7:
            key = KC_HOME;
            break;
        case 2:
            key = KC_INS;
            break;
        case 3:
            key = KC_DEL;
            break;
        case 4:
        case 8:
            key = KC_END;
            break;
        case 10:
            key = KC_F0;
            break;
        case 11:
            key = KC_F1;
            break;
        case 12:
            key = KC_F2;
            break;
        case 13:
            key = KC_F3;
            break;
        case 14:
            key = KC_F4;
            break;
        case 15:
            key = KC_F5;
            break;
        case 17:
            key = KC_F6;
            break;
        case 18:
            key = KC_F7;
            break;
        case 19:
            key = KC_F8;
            break;
        case 20:
            key = KC_F9;
            break;
        case 21:
            key = KC_F10;
            break;
        case 23:
            key = KC_F11;
            break;
        case 24:
            key = KC_F12;
            break;
        case 25:
            key = KC_F13;
            break;
        case 26:
            key = KC_F14;
            break;
        case 28:
            key = KC_F15;
            break;
        case 29:
            key = KC_F16;
            break;
        case 31:
            key = KC_F17;
            break;
        case 32:
            key = KC_F18;
            break;
        case 33:
            key = KC_F19;
            break;
        case 34:
            key = KC_F20;
            break;
    }
    if(c == '~') {
        e->key = key;
        e->modifier = 0;
        return 1;
    }

    // invalid sequence
    if(c != ';') return -2;

    unsigned char mod = 0;
    // this will read 1 char past the end of the number
    ret = readuc(fd, &mod);
    if(ret == -1) return -1;
    // incomplete char code
    if(ret == INT_MAX) return -2;
    c = (unsigned char)ret;

    if(c != '~') return -2;

    if(mod == 0) return -1;

    e->key = key;
    e->modifier = mod -1;

    return 1;
}

int keyevent_fmt(struct KeyEvent *e, char *buff, size_t len) {
    size_t off = 0;

    int is_shift = KM_Shift & e->modifier;

    if(e->modifier) {
        if(buff) {
            if(off == len) return -1;
            buff[off] = '<';
        }
        off+=1;
    }

    if(KM_Alt & e->modifier) {
        if(buff) {
            if(off + 4 >= len) return -1;
            memcpy(buff + off, "Alt-", 4);
        }
        off+=4;
    }

    if(KM_Ctrl & e->modifier) {
        if(buff) {
            if(off + 5 >= len) return -1;
            memcpy(buff + off, "Ctrl-", 5);
        }
        off+=5;
    }

    if(KM_Meta & e->modifier) {
        if(buff) {
            if(off + 5 >= len) return -1;
            memcpy(buff + off, "Meta-", 5);
        }
        off+=5;
    }

    if(is_shift && !isalpha(e->key)) {
        if(buff) {
            if(off + 6 >= len) return -1;
            memcpy(buff + off, "Shift-", 6);
        }
        off+=6;
    }

#define CHECKED_CPY(s) \
    if(buff) { \
        if(off + sizeof(s) >= len) return -1; \
        memcpy(buff + off, s, sizeof(s)); \
    } \
    off += sizeof(s); \
    break;


    switch(e->key) {
        case 'a' ... 'z':
            if(is_shift) {
                e->key = towlower(e->key);
            }
            // fall through
        case 'A' ... 'Z':
            if(buff) {
                if(off + 1 >= len) return -1;
                buff[off] = (char)e->key;
            }
            off += 1;
            break;
        case '\n':
            if(buff) {
                if(off + 2 >= len) return -1;
                buff[off] = '\\';
                buff[off+1] = 'n';
            }
            off += 2;
            break;
        case '\e':
            if(buff) {
                if(off + 2 >= len) return -1;
                buff[off] = '\\';
                buff[off+1] = 'e';
            }
            off += 2;
            break;
        case KC_DEL:
            CHECKED_CPY("Del");
        case KC_HOME:
            CHECKED_CPY("Home");
        case KC_INS:
            CHECKED_CPY("Ins");
        case KC_END:
            CHECKED_CPY("End");
        case KC_PGUP:
            CHECKED_CPY("PgUp");
        case KC_PGDN:
            CHECKED_CPY("PgDn");
        case KC_F0:
            CHECKED_CPY("F0");
        case KC_F1:
            CHECKED_CPY("F1");
        case KC_F2:
            CHECKED_CPY("F2");
        case KC_F3:
            CHECKED_CPY("F3");
        case KC_F4:
            CHECKED_CPY("F4");
        case KC_F5:
            CHECKED_CPY("F5");
        case KC_F6:
            CHECKED_CPY("F6");
        case KC_F7:
            CHECKED_CPY("F7");
        case KC_F8:
            CHECKED_CPY("F8");
        case KC_F9:
            CHECKED_CPY("F9");
        case KC_F10:
            CHECKED_CPY("F10");
        case KC_F11:
            CHECKED_CPY("F11");
        case KC_F12:
            CHECKED_CPY("F12");
        case KC_F13:
            CHECKED_CPY("F13");
        case KC_F14:
            CHECKED_CPY("F14");
        case KC_F15:
            CHECKED_CPY("F15");
        case KC_F16:
            CHECKED_CPY("F16");
        case KC_F17:
            CHECKED_CPY("F17");
        case KC_F18:
            CHECKED_CPY("F18");
        case KC_F19:
            CHECKED_CPY("F19");
        case KC_F20:
            CHECKED_CPY("F20");
        case KC_ARRUP:
            CHECKED_CPY("Up");
        case KC_ARRDOWN:
            CHECKED_CPY("Down");
        case KC_ARRRIGHT:
            CHECKED_CPY("Right");
        case KC_ARRLEFT:
            CHECKED_CPY("Left");
        default: {
            int utf_size = utf32_len_utf8(e->key);
            if(utf_size > 1) {
                if(buff) {
                    utf32_to_utf8(e->key, buff + off, len - off - utf_size);
                }
                off += utf_size;
            } else if(isprint(e->key) && !isspace(e->key)) {
                if(buff) {
                    if(off + 1 >= len) return -1;
                    if(buff) {
                        buff[off] =  e->key;
                    }
                }
                off += 1;
            } else {
                size_t size = snprintf(NULL, 0, "0x%x", e->key);
                if(buff) {
                    if(off-1 + size >= len) return -1;
                }
                off += size;
            }
        }
    }

    if(e->modifier) {
        if(buff) {
            if(off + 1 >= len) return -1;
            buff[off] = '>';
        }
        off+=1;
    }

    if(buff) {
        if(off + 1 >= len) return -1;
        buff[off] = '\0';
    }

    return off;
}

int cmp_keys(
        struct KeyEvent *restrict e,
        int modifiers,
        enum KeyCode k) {
    return e->modifier == modifiers && e->key == k;
}

#ifdef TESTING

#include "tests.h"
#include <stdio.h>

TESTS_START

TEST_DEF(readkey_japanese)
    FILE *f = tmpfile();
    if(!f) {
        TEST_ASSERT(f && "tmp file could not be opened");
    } else {
        int fno = fileno(f);
        struct KeyEvent e = {0};
        int ret = write(fno, "アイドル", 12);
        TEST_ASSERT(ret == 12);
        rewind(f);

        ret = readkey(fno, &e);
        TEST_ASSERT(ret == 1);
        TEST_ASSERT(e.key = 12450);

        ret = readkey(fno, &e);
        TEST_ASSERT(ret == 1);
        TEST_ASSERT(e.key = 12452);
    }
TEST_ENDDEF


TEST_DEF(keyevent_fmt)
    #define BUF_LEN 10
    char buf[BUF_LEN] = {0};

    struct KeyEvent e = {
        .modifier = 0,
        .key = 23454,
    };

    int len = keyevent_fmt(&e, buf, BUF_LEN);
    TEST_ASSERT(len == 3);

    const char *expected = "实";

    // assert that the null terminator was written
    TEST_ASSERT(!strncmp(buf, expected, 4));

    e = (struct KeyEvent) {
        .modifier = KM_Meta,
        .key = 23,
    };

    // pass in a buffer that's too small
    len = keyevent_fmt(&e, buf, BUF_LEN);
    TEST_ASSERT(len < 0);
    // pass in a null buffer and get the expected size (minus null terminator)
    len = keyevent_fmt(&e, 0, BUF_LEN);
    TEST_ASSERT(len == 11);

TEST_ENDDEF

TESTS_END

#endif
