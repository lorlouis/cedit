#ifndef XALLOC_H
#define XALLOC_H 1

#include <stddef.h>
#include <assert.h>

#define TODO(msg) assert(0 && #msg)

// Allocs `s` bytes or panics
void *xmalloc(size_t s);

// Callocs `s` bytes or panics
void *xcalloc(size_t count, size_t size);

// Reallocs ptr or panics
void *xrealloc(void *ptr, size_t size);

#endif
