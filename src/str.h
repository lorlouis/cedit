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

void vec_extend(Vec *v, const void *data, size_t size);

void vec_cleanup(Vec *v);

#define VEC_NEW(type, teardown_fn) (Vec) { \
    .buf = 0, \
    .len = 0, \
    .cap = 0, \
    .type_size = sizeof(type), \
    .free_fn = teardown_fn, \
}

void vec_clear(Vec *v);

void* vec_get(const Vec *v, size_t idx);

// Returns 1 if there was something to pop
int vec_pop(Vec *v, void *out);

void vec_push(Vec *v, void *data);

int vec_insert(Vec *v, size_t idx, void *data);

#define VEC_GET(type, v, idx) (type*)vec_get(v, idx)

typedef struct {
    Vec v;
    Vec char_pos;
} Str;

Str str_new(void);

void str_free(Str *s);

Vec* str_as_vec(Str *s);

int str_push(Str *s, const char *o, size_t len);

void str_clear(Str *s);

void str_trunc(Str *s, size_t new_len);

size_t str_len(const Str *s);

size_t str_cstr_len(const Str *s);

int str_insert_at(Str *s, size_t idx, const char *o, size_t len);

size_t str_size(Str *s);

size_t str_get_char_byte_idx(const Str *s, size_t idx);

const char* str_as_cstr(const Str *s);

Str str_from_cstr(const char *s);

Str str_from_cstr_len(const char *s, size_t len);

Str str_clone(const Str *s);

// Equivalent to idx + <char*> on a cstr
const char* str_tail_cstr(const Str *s, size_t idx);

// Returns a readonly Str containing the chars after char idx
Str str_tail(const Str *s, size_t idx);

Str str_head(const Str *s, size_t idx);

int str_remove(Str *s, size_t start, size_t end);

int str_is_empty(Str *s);

#ifdef UTF_H
    int str_get_char(const Str *s, size_t idx, utf32 *out);
#endif

#endif
