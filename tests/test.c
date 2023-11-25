#include "vt.h"

#include "tests.h"

#include "utf.h"

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

TESTS_END
