#include "tests.h"

#include "vt.h"
#include "utf.h"

#include "str.h"
#include <string.h>

#define STR_SIZE(s) s, (sizeof(s)-1)

TESTS_START

load_locale();

TEST_DEF(test_take_cols)
    char line[] = "this is a test";
    size_t nb_cols = 4;
    ssize_t ret = take_cols(line, sizeof(line), &nb_cols, 4);
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
    vec_extend(&v, data+2, sizeof(int));
    ASSERT(VEC_GET(int, &v, 4) != 0);
    ASSERT(*VEC_GET(int, &v, 4) == 4);
    ASSERT(VEC_GET(int, &v, 14) == 0);
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
    ASSERT(str_get_char(&s, 30, &c) == -1);
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
