#include <string.h>
#include "xalloc.h"
#include <assert.h>
#include <errno.h>

#include "str.h"
#include "utf.h"

const char *EMPTY_STR = "";

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

int vec_is_empty(Vec *v) {
    return v->len == 0;
}

void vec_extend(Vec *v, const void *data, size_t count) {
    assert(v->type_size && "uninitialised vector");
    vec_grow_to_fit(v, count + v->len);
    if(data >= v->buf && data <= v->buf + v->len * v->type_size) {
        memmove(v->buf+ v->len * v->type_size, data, count * v->type_size);
    } else {
        memcpy(v->buf + v->len * v->type_size, data, count* v->type_size);
    }
    v->len += count;
    return;
}

// Returns 1 if there was something to pop
int vec_pop(Vec *v, void *out) {
    if(v->len == 0){
        return 0;
    }
    void *las_elem = vec_get(v, v->len-1);
    if(out) {
        memcpy(out, las_elem, v->type_size);
    } else if (v->free_fn) {
        v->free_fn(las_elem);
    }
    v->len -= 1;
    return 1;
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
    s.char_pos.type_size = sizeof(size_t);
    return s;
}

Vec* str_as_vec(Str *s) {
    return &s->v;
}

static int build_character_table(Vec *ct, size_t start_off, const char *s, size_t len) {
    size_t i = start_off;
    while(i < len) {
        int byte_count = utf8_byte_count(s[i]);
        if(byte_count < 1) return -1;
        size_t entry = i+start_off;
        vec_push(ct, &entry);
        i += byte_count;
    }
    return 0;
}

static int is_ascii(const char *s, size_t len) {
    int ascii_only = 1;
    for(size_t i = 0; i < len; i++) {
        if((s[i] & 0b11000000) >= 0b10000000) {
            ascii_only = 0;
            break;
        }
    }
    return ascii_only;
}

int str_push(Str *s, char const *o, size_t len) {
    if(len == 0) return 0;
    s->v.type_size = sizeof(char);
    size_t original_len = s->v.len;

    vec_pop(&s->v, NULL);
    vec_extend(&s->v, o, len);

    if(*VEC_GET(char, &s->v, s->v.len-1) != '\0') {
        char terminator = '\0';
        vec_push(&s->v, &terminator);
    }

    int ascii_only = s->char_pos.len == 0 && is_ascii(o, len);

    if(!ascii_only) {
        int start_off = 0;
        if(s->char_pos.len) {
            start_off = original_len;
        }
        int ret = build_character_table(
                &s->char_pos,
                start_off,
                str_as_cstr(s),
                str_cstr_len(s)
            );

        if(ret) {
            return ret;
        }
    }
    return 0;
}

void str_clear(Str *s) {
    s->v.type_size = sizeof(char);
    if(s->v.buf && s->v.cap >= 1) {
        ((char*)s->v.buf)[0] = '\0';
    }
    s->v.len = 0;
    return;
}

void str_free(Str *s) {
    str_clear(s);
    vec_cleanup(&s->v);
}

void str_trunc(Str *s, size_t new_len) {
    s->v.type_size = sizeof(char);
    if(new_len >= s->v.len) return;
    ((char*)s->v.buf)[new_len+1] = '\0';
    s->v.len = new_len+1;
    return;
}

int str_insert_at(Str *s, size_t idx, const char *o, size_t len) {
    if(idx == str_len(s)) {
        return str_push(s, o, len);
    }

    vec_grow_to_fit(&s->v, str_size(s) + len);
    size_t byte_idx = str_get_char_byte_idx(s, idx);
    size_t overlap_size = str_size(s) - byte_idx;
    // move the overlapping part to the end of the line
    memmove(s->v.buf + byte_idx + len, s->v.buf + byte_idx, overlap_size);
    // copy o into the overlap
    memmove(s->v.buf + byte_idx, o, len);
    s->v.len += len;

    if(!vec_is_empty(&s->char_pos) || !is_ascii(o, len) ) {
        // TODO(louis) do the proper thing and do not recompute the whole index table
        vec_clear(&s->char_pos);
        int ret = build_character_table(
                &s->char_pos,
                0,
                str_as_cstr(s),
                str_cstr_len(s)
            );
        if(ret) return ret;
    }
    return 0;
}

// Returns the size (in characters) of the string including `NULL` character.
size_t str_size(Str *s) {
    return s->v.len;
}

// Returns the size (in characters) of the string excluding `NULL` character.
size_t str_cstr_len(const Str *s) {
    if(s->v.len == 0) return 0;
    return s->v.len -1;
}

// Returns the number of UTF-8 code points.
size_t str_len(Str *s) {
    if(s->char_pos.buf) return s->char_pos.len;
    if(s->v.len == 0) return 0;
    return s->v.len -1;
}

size_t str_get_char_byte_idx(Str *s, size_t idx) {
    if(s->char_pos.buf) {
        size_t *new_idx = VEC_GET(size_t, &s->char_pos, idx);
        if(new_idx) return *new_idx;
        return -1;
    } else if (idx <= s->v.len) return idx;
    return -1;
}

int str_get_char(Str *s, size_t idx, utf32 *out) {
    size_t index = str_get_char_byte_idx(s, idx);
    if(index == (size_t)-1) return -1;
    if(utf8_to_utf32(str_as_cstr(s) + index, s->v.len - index, out) < 0) return -1;
    return 0;
}

const char* str_as_cstr(const Str *s) {
    if(s->v.len == 0) return EMPTY_STR;
    return (char*)s->v.buf;
}

// Deep clone, don't forget to free
Str str_clone(const Str *s) {
    Str new = str_from_cstr_len(str_as_cstr(s), str_cstr_len(s));
    return new;
}

Str str_from_cstr(const char *s) {
    Str new = str_new();
    size_t s_len = strlen(s);
    str_push(&new, s, s_len);
    return new;
}

Str str_from_cstr_len(const char *s, size_t len) {
    Str new = str_new();
    str_push(&new, s, len);
    return new;
}

int str_is_empty(Str *s) {
    return vec_is_empty(&s->v);
}
