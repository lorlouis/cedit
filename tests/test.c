#include "vt.h"

#include "tests.h"

TESTS_START

TEST_START(test_take_cols)
    char line[] = "this is a test";
    size_t nb_cols = 4;
    ssize_t ret = take_cols(line, sizeof(line), &nb_cols, 4);
    ASSERT(ret == 4);
TEST_END

TESTS_END
