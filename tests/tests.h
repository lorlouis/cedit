#ifndef TESTS_H
#define TESTS_H 1

#include <stdio.h>

#define STRINGIFY(s) XSTRINGIFY(s)
#define XSTRINGIFY(s) #s

#define TESTS_START \
void failed_assert(void) { \
    return; \
} \
int main() { \
    int test_count = 0; \
    int failed_tests_count = 0; \
    int status = 0;

#define TEST_DEF(name) \
    { \
        char *test_name = STRINGIFY(name); \
        test_count += 1; \
        status = 0; \
        printf("\033[0;33mRunning\033[0m %s", test_name);

#define ASSERT(condition) \
        if(!status && !(condition)) { \
            status = 1; \
            printf("\n\t%s:%d: `%s` is false", __FILE__, __LINE__, STRINGIFY(condition)); \
            failed_assert(); \
        }

#define TEST_ENDDEF \
        if(status) { \
            puts("\t\033[0;31mFAIL\033[0m"); \
            failed_tests_count += 1; \
        } else { \
            puts("\t\033[0;32mPASS\033[0m"); \
        } \
    }

#define TESTS_END \
    printf("\n\tRan %d tests\t%d Failed\n", test_count, failed_tests_count); \
    return failed_tests_count != 0; \
}

#endif
