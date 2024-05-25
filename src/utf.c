#include "utf.h"

#include <assert.h>
#define __USE_XOPEN // required for wcwidth to be defined
#include <wchar.h>
#include <stdlib.h>
#include <locale.h>

#include "config.h"

#include "xalloc.h"

int load_locale(void) {
    char *lc_ctype = getenv("LC_CTYPE");
    if(lc_ctype) {
        if(!setlocale(LC_CTYPE, lc_ctype)) return -1;
        return 0;
    }

    char *lang = getenv("LANG");

    if(lang) {
        if(!setlocale(LC_ALL, lang)) return -1;
        return 0;
    }
    return 0;
}

int utf8_is_follow(utf8 c) {
    return (c & 0b11000000) == 0b10000000;
}

int utf8_byte_count(utf8 c) {
    if((c & 0b11111000) == 0b11110000) return 4;
    if((c & 0b11110000) == 0b11100000) return 3;
    if((c & 0b11100000) == 0b11000000) return 2;
    // this would be a follow byte, ie invalid
    if((c & 0b11000000) == 0b10000000) return -1;
    return 1;
}

int utf32_len_utf8(utf32 c) {
    switch(c) {
        case 0x000000 ... 0x00007f:
            return 1;
        case 0x000080 ... 0x0007ff:
            return 2;
        case 0x000800 ... 0x00ffff:
            return 3;
        case 0x010000 ... 0x10ffff:
            return 4;
        default:
            return -1;
    }
}

int utf8_to_utf32(const utf8 *s, size_t len, utf32 *out) {
    if(!s || !len) return 0;
    if(*s == '\0') {
        *out = 0;
        return 0;
    }

    int byte_count = utf8_byte_count(*s);
    if(byte_count < 0 || len < (size_t)byte_count) return -1;
    utf32 code = 0;
    // extract the value present in the first byte
    switch(byte_count) {
        case 4:
            code |= (0b00000111 & *s);
            break;
        case 3:
            code |= (0b00001111 & *s);
            break;
        case 2:
            code |= (0b00011111 & *s);
            break;
        case 1:
            code |= (0b01111111 & *s);
            break;
    }
    // read the follow bytes
    for(size_t i = 1; i < (size_t)byte_count; i++) {
        code <<= 6;
        // check for invalid follow byte
        if(s[i] & 0b01000000) return -1;
        code |= (0b00111111 & s[i]);
    }
    *out = code;
    return byte_count;
}

int utf32_to_utf8(utf32 c, utf8 *buff, size_t size) {
    int byte_count = utf32_len_utf8(c);
    if(byte_count < 1 || (size_t)byte_count > size) return -1;
    switch(byte_count) {
        case 4:
            *buff = 0b11110000;
            *buff |= ((0b00000111 << 18) & c) >> 18;
            break;
        case 3:
            *buff = 0b11100000;
            *buff |= ((0b00001111 << 12) & c) >> 12;
            break;
        case 2:
            *buff = 0b11000000;
            *buff |= ((0b00011111 << 6) & c) >> 6;
            break;
        case 1:
            *buff = (utf8)c;
            return 1;
    }
    utf32 mask = 0b00111111 << ((byte_count-2) * 6);
    for(size_t i = 1; i < (size_t)byte_count; i++) {
        buff[i] = 0b10000000;
        buff[i] |= (mask & c) >> ((byte_count-1-i) * 6);
        mask >>= 6;
    }
    return byte_count;
}

int utf8_to_utf32(const utf8 *s, size_t len, utf32 *out);

wint_t utf32_to_wint(utf32 c) {
    static_assert(sizeof(wchar_t) == 2 || sizeof(wchar_t) == 4, "Unsupported wchar_t size");
    char buff[4] = {0};
    wchar_t out = 0;
    if(sizeof(wchar_t) == 4) {
        if(utf32_to_utf8(c, buff, sizeof(buff)) < 0) return -1;
    } else {
        TODO(implement to utf16);
    }

    // initialise internal state
    mbtowc(&out, 0, 0);
    if(mbtowc(&out, buff, sizeof(buff)) < 0) return -1;

    return out;
}

int utf32_width(utf32 c) {
    wint_t wc = utf32_to_wint(c);
    int width = wcwidth(wc);
    if(width == -1) {
        switch(wc) {
            case L'\t':
                return CONFIG.tab_width;
            default: {}
                //assert(0 && "unknown variant");
        }
    }
    return width;
}

utf32 wint_to_utf32(wint_t c);

#ifdef TESTING

#include <string.h>
#include "str.h"
#include "tests.h"
#include "vt.h"

#define STR_SIZE(s) s, (sizeof(s)-1)

TESTS_START

load_locale();

TEST_DEF(test_take_cols)
    Str line = str_from_cstr("this is a test");
    size_t nb_cols = 4;
    ssize_t ret = take_cols(&line, &nb_cols, 4);
    str_free(&line);
    ASSERT(ret == 4);
TEST_ENDDEF

TEST_DEF(test_utf8_codepoint_ascii)
{
    char *s = "$";
    utf32 c = 0;
    ASSERT(utf8_to_utf32(s, 1, &c) == 1);
    ASSERT(c == 0x24);
}
{
    char *s = "$2";
    utf32 c = 0;
    ASSERT(utf8_to_utf32(s, 2, &c) == 1);
    ASSERT(c == 0x24);
}
TEST_ENDDEF

TEST_DEF(test_utf8_codepoint_multi_bytes)
    char *s = "£";
    utf32 c = 0;
    ASSERT(utf8_to_utf32(s, 2, &c) == 2);
    ASSERT(c == 0xa3);
TEST_ENDDEF

TEST_DEF(test_utf8_invalid_follow)
    char s[] = {0xc2, 0xc0};
    utf32 c = 1;
    ASSERT(utf8_to_utf32(s, 2, &c) == -1);
    ASSERT(c == 1);
TEST_ENDDEF

TEST_DEF(test_utf8_lenght_too_short)
    char s = 0xc2;
    utf32 c = 1;
    ASSERT(utf8_to_utf32(&s, 1, &c) == -1);
    ASSERT(c == 1);
TEST_ENDDEF

TEST_DEF(test_utf8_read_kanji)
    char s[] = "増a";
    utf32 c = 1;
    ASSERT(utf8_to_utf32(s, sizeof(s)-1, &c) == 3);
    ASSERT(c == 0x5897);
TEST_ENDDEF

TEST_DEF(test_vec_operations)
    Vec v = VEC_NEW(int, 0);
    int data[] = {1 ,2 ,4 ,5};
    vec_extend(&v, data, 4);
    ASSERT(!memcmp(v.buf, data, sizeof(data)));
    ASSERT(v.len == sizeof(data) / sizeof(int));
    ASSERT(v.cap == 4);
    vec_extend(&v, data+2, 1);
    ASSERT(VEC_GET(int, &v, 4) != 0);
    ASSERT(*VEC_GET(int, &v, 4) == 4);
    ASSERT(v.len == 5);
    vec_insert(&v, 0, data+3);
    int data2[] = {5, 1 ,2 ,4 ,5, 4};
    ASSERT(!memcmp(v.buf, data2, sizeof(data2)));
    vec_cleanup(&v);
TEST_ENDDEF

TEST_DEF(str_operations)
    Str s = str_new();
    ASSERT(!strcmp(str_as_cstr(&s), ""));
    str_push(&s, STR_SIZE("this is atest"));
    ASSERT(!strcmp(s.v.buf, "this is atest"));
    str_push(&s, STR_SIZE(" wowo"));
    ASSERT(!strcmp(s.v.buf, "this is atest wowo"));
TEST_ENDDEF

TEST_DEF(str_utf8_fuckery)
    Str s = str_new();
    ASSERT(str_push(&s, STR_SIZE("hello world")) != -1);
    ASSERT(s.char_pos.buf == 0);
    ASSERT(str_push(&s, STR_SIZE(" 計算(keisan)")) != -1);
    ASSERT(s.char_pos.buf != 0);

    utf32 c = 0;
    ASSERT(str_get_char(&s, 13, &c) == 0);
    ASSERT(c == 0x7b97);
TEST_ENDDEF

TEST_DEF(test_code_point_to_utf8)
{
    utf32 code = 'a';
    char s[4] = {0};
    ASSERT(utf32_to_utf8(code, s, 4) == 1);
    ASSERT(!strcmp(s, "a"));
}
{
    utf32 code = 0x00a3;
    char s[4] = {0};
    ASSERT(utf32_to_utf8(code, s, 4) == 2);
    ASSERT(!strcmp(s, "£"));
}
{
    utf32 code = 0x20ac;
    char s[4] = {0};
    ASSERT(utf32_to_utf8(code, s, 4) == 3);
    ASSERT(!strcmp(s, "€"));
}
{
    utf32 code = 0xe9;
    char s[4] = {0};
    ASSERT(utf32_to_utf8(code, s, 4) == 2);
    ASSERT(!strcmp(s, "é"));
}
{
    utf32 code = 0x10348;
    char s[4] = {0};
    ASSERT(utf32_to_utf8(code, s, 4) == 4);
    ASSERT(!strncmp(s, "𐍈", 4));
}
TEST_ENDDEF

TEST_DEF(test_utf_to_wchar_and_back)
    utf32 code = 0x00a3;
    wint_t c = utf32_to_wint(code);
    ASSERT(c != (wint_t)-1);
    ASSERT(L'£' == c);
TEST_ENDDEF

TESTS_END

#endif
