#include <string.h>
#include "xalloc.h"
#include <assert.h>
#include <errno.h>

#include "str.h"
#include "utf.h"

static void vec_grow_to_fit(Vec *v, size_t count) {
    if(v->cap < v->len + count) {
        size_t new_cap = v->cap ? v->cap * 2 : 2;
        while(new_cap < v->len + count) {
            new_cap *= 2;
        }
        v->buf = xrealloc(v->buf, new_cap * v->type_size);
        v->cap = new_cap;
    }
}

void vec_extend(Vec *v, void *data, size_t count) {
    assert(v->type_size && "uninitialised vector");
    vec_grow_to_fit(v, count + v->len);
    if(data >= v->buf && data <= v->buf + v->len * v->type_size) {
        memmove(v->buf+v->len * v->type_size, data, count * v->type_size);
    } else {
        memcpy(v->buf+v->len * v->type_size, data, count* v->type_size);
    }
    v->len += count;
    return;
}

void vec_push(Vec *v, void *data) {
    vec_extend(v, data, 1);
    return;
}

int vec_insert(Vec *v, size_t idx, void *data) {
    if(idx > v->len) return -EINVAL;
    if(idx == v->len) {
        vec_push(v, data);
        return 0;
    }
    vec_grow_to_fit(v, v->len + 1);
    memmove(vec_get(v, idx+1), vec_get(v, idx), v->type_size * (v->len - idx));
    memmove(vec_get(v, idx), data, v->type_size);
    return 0;
}

void vec_cleanup(Vec *v) {
    vec_clear(v);
    if(v->buf) xfree(v->buf);
    return;
}

void vec_clear(Vec *v) {
    if(v->free_fn) {
        for(size_t i = 0; i < v->len; i++) {
            v->free_fn(v->buf + i * v->type_size);
        }
    }
    memset(v->buf, 0, v->cap * v->type_size);
    v->len = 0;
    return;
}

void* vec_get(Vec *v, size_t idx) {
    assert(v->type_size && "uninitialised vector");
    if(idx > v->len) return 0;
    return v->buf + idx * v->type_size;
}

Str str_new(void) {
    Str s = {0};
    s.v.type_size = sizeof(char);
    return s;
}

Vec* str_as_vec(Str *s) {
    return &s->v;
}

void str_push(Str *s, char *o, size_t len) {
    s->v.type_size = sizeof(char);
    vec_extend(&s->v, o, len + 1);

    _Bool contains_utf8 = s->char_pos.len != 0;
    if(!contains_utf8) {
        for(size_t i = 0; i < len; i++) {
            if((o[i] & 0b11000000) == 0b10000000) {
                contains_utf8 = 1;
                break;
            }
        }
    }
    if(contains_utf8) {
        if(s->char_pos.len) {
            for(size_t i = 0; i < len; i++) {
            }
        }
    }

    return;
}

void str_clear(Str *s) {
    s->v.type_size = sizeof(char);
    if(s->v.buf && s->v.cap >= 1) {
        ((char*)s->v.buf)[0] = '\0';
    }
    s->v.len = 0;
    return;
}

void str_trunc(Str *s, size_t new_len) {
    s->v.type_size = sizeof(char);
    if(new_len >= s->v.len) return;
    s->v.len = new_len;
    ((char*)s->v.buf)[new_len+1] = '\0';
    return;
}

size_t str_size(Str *s) {
    return s->v.len;
}

size_t str_len(Str *s) {
    if(s->char_pos.buf) return s->char_pos.len;
    return s->v.len;
}

size_t str_get_char_byte_idx(Str *s, size_t idx) {
    if(s->char_pos.buf) {
        size_t *new_idx = VEC_GET(size_t, &s->v, idx);
        if(new_idx) return *new_idx;
        return -1;
    } else if (idx <= s->v.len) return idx;
    return -1;
}

int str_get_char(Str *s, size_t idx, utf32 *out) {
    size_t index = str_get_char_byte_idx(s, idx);
    if(index == (size_t)-1) return -1;
    if(utf8_to_utf32(str_as_cstr(s), s->v.len, out) < 0) return -1;
    return 0;
}

char* str_as_cstr(Str *s) {
    return (char*)s->v.buf;
}
