#ifndef TESTS_H
#define TESTS_H 1

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <assert.h>

#define STRINGIFY(s) XSTRINGIFY(s)
#define XSTRINGIFY(s) #s

#define TESTS_START \
_Bool generate_trap = 1; \
int main(int argc, char **argv) { \
    printf("\033[0;33mRunning %s's tests\033[0m\n", __FILE__); \
    { \
        int test_c = 0; \
        while((test_c = getopt(argc, argv, "t")) != -1) { \
            switch(test_c) { \
                case 't': \
                    generate_trap = 1; \
                    break; \
                default: \
                    fprintf(stderr, "unknown argument -%c\n", test_c); \
                    return 1; \
            } \
        } \
    } \
    int test_count = 0; \
    int failed_tests_count = 0; \
    int status = 0;

#define TEST_DEF(name) \
    { \
        char *test_name = STRINGIFY(name); \
        test_count += 1; \
        status = 0; \
        printf("\033[0;33mRunning\033[0m %s", test_name); \
        fflush(stdout); \
        pid_t pid = fork(); \
        assert(pid >= 0); \
        if(!pid) {

#define ASSERT(condition) \
        if(!status && !(condition)) { \
            status = 1; \
            printf("\n\t%s:%d: `%s` is false", __FILE__, __LINE__, STRINGIFY(condition)); \
            if(generate_trap) raise(SIGTRAP); \
        }

#define TEST_ENDDEF \
            exit(status); \
        } \
        assert(waitpid(pid, &status, 0)); \
        int exit_code = 0; \
        if(WIFEXITED(status)) exit_code = WEXITSTATUS(status); \
        if(exit_code || WIFSIGNALED(status)) { \
            puts("\t\033[0;31mFAIL\033[0m"); \
            failed_tests_count += 1; \
        } else { \
            puts("\t\033[0;32mOK\033[0m"); \
        } \
        fflush(stdout); \
    }

#define TESTS_END \
    printf("\tRan %d tests\t%d Failed\n", test_count, failed_tests_count); \
    return failed_tests_count != 0 && status == 0; \
}

#endif
