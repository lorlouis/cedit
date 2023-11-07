#include "vt.h"

#include <stdio.h>
#include <assert.h>

void test_take_cols(void) {
    char line[] = "this is a test";
    size_t nb_cols = 4;
    ssize_t ret = take_cols(line, sizeof(line), &nb_cols, 4);

    assert(ret == 4);
    printf("[OK]\n");
    return;
}

int main(int argc, const char **argv) {
    test_take_cols();
    return 0;
}
