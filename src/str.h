#ifndef STR_H
#define STR_H 1

#include <stddef.h>

typedef struct {
    void *buf;
    size_t len;
    size_t cap;
    size_t type_size;
    void (*free_fn)(void*);
} Vec;

void vec_extend(Vec *v, void *data, size_t size);

void vec_cleanup(Vec *v);

#define VEC_NEW(type, teardown_fn) (Vec) { \
    .buf = 0, \
    .len = 0, \
    .cap = 0, \
    .type_size = sizeof(type), \
    .free_fn = teardown_fn, \
}

void vec_clear(Vec *v);

void* vec_get(Vec *v, size_t idx);

int vec_insert(Vec *v, size_t idx, void *data);

#define VEC_GET(type, v, idx) (type*)vec_get(v, idx)

typedef struct {
    Vec v;
    Vec char_pos;
} Str;

Str str_new(void);

Vec* str_as_vec(Str *s);

void str_push(Str *s, char *o, size_t len);

void str_clear(Str *s);

void str_trunc(Str *s, size_t new_len);

size_t str_len(Str *s);
size_t str_size(Str *s);

size_t str_get_char_byte_idx(Str *s, size_t idx);

char* str_as_cstr(Str *s);

#ifdef UTF_H
    utf32 str_get_char(Str *s, size_t idx);
#endif

#endif
