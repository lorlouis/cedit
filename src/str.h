#ifndef STR_H
#define STR_H 1

#include <stddef.h>

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} Str;

void str_push(Str *s, char *o, size_t len);

void str_clear(Str *s);

void str_trunc(Str *s, size_t new_len);

#endif

