#include "vt.h"

#include "tests.h"

#include "utf.h"

#include "str.h"
#include <string.h>

TESTS_START

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
    ASSERT(!strcmp(&s, ""));
    str_push(&s, "this is atest", 14);
    ASSERT(!strcmp(&s, "this is atest"));
TEST_ENDDEF

TESTS_END
