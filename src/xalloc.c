#include <stdlib.h>
#include <assert.h>

void *xmalloc(size_t s) {
    void *m = malloc(s);
    assert(m && "xmalloc: ran out of memory");
    return m;
}

void *xcalloc(size_t count, size_t size) {
    void *m = calloc(count, size);
    assert(m && "xcalloc: ran out of memory");
    return m;
}

void *xrealloc(void *ptr, size_t size) {
    void *m = realloc(ptr, size);
    if(!m) free(ptr);
    assert(m && "xrealloc: ran out of memory");
    return m;
}

void xfree(void *ptr) {
    free(ptr);
    return;
}
