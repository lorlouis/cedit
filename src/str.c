#include <string.h>
#include "xalloc.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include "utf.h"
#include "str.h"

const char *EMPTY_STR = "";

void vec_grow_to_fit(Vec *v, size_t count) {
    assert(v->cap != SIZE_MAX && "vec is readonly");
    if(v->cap < v->len + count) {
        size_t new_cap = v->cap ? v->cap * 2 : 2;
        while(new_cap < v->len + count) {
            new_cap *= 2;
        }
        v->buf = xrealloc(v->buf, new_cap * v->type_size);
        v->cap = new_cap;
    }
}

int vec_is_empty(const Vec *v) {
    return v->len == 0;
}

void vec_trunk(Vec *v, size_t len) {
    assert(len <= v->len && "index out of range");

    if(v->free_fn) {
        for(size_t i = len; i < v->len; i++) {
            v->free_fn(vec_get(v, i));
        }
    }

    v->len = len;
}

void vec_extend(Vec *v, const void *data, size_t count) {
    assert(v->cap != SIZE_MAX && "vec is readonly");
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
    assert(v->cap != SIZE_MAX && "vec is readonly");

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

// Returns a readonly vec representing the elements [idx...]
Vec vec_tail(const Vec *v, size_t idx) {
    assert(idx+1 <= v->len && "index out of range");
    Vec tail = *v;
    tail.buf += idx * v->type_size;
    tail.cap = SIZE_MAX;
    tail.len -= idx;
    return tail;
}

void vec_remove(Vec *v, size_t idx) {
    assert(v->cap != SIZE_MAX && "vec is readonly");
    assert(idx < v->len && "index out of range");
    if(idx == v->len-1) {
        vec_pop(v, 0);
        return;
    }

    if(v->free_fn) {
        v->free_fn(vec_get(v, idx));
    }

    memmove(
            vec_get(v, idx),
            vec_get(v, idx + 1),
            (v->len - (idx+1)) * v->type_size);
    v->len -= 1;
    return;
}

int vec_insert(Vec *v, size_t idx, void *data) {
    assert(v->cap != SIZE_MAX && "vec is readonly");
    if(idx > v->len) return -EINVAL;

    if(idx == v->len) {
        vec_push(v, data);
        return 0;
    }
    vec_grow_to_fit(v, v->len + 1);
    v->len += 1;
    memmove(vec_get(v, idx+1), vec_get(v, idx), v->type_size * (v->len - idx));
    memmove(vec_get(v, idx), data, v->type_size);
    return 0;
}

void vec_cleanup(Vec *v) {
    if(v->cap == SIZE_MAX) return;
    vec_clear(v);
    if(v->buf) xfree(v->buf);
    v->buf = 0;
    return;
}

void vec_clear(Vec *v) {
    assert(v->cap != SIZE_MAX && "vec is readonly");
    if(v->free_fn) {
        for(size_t i = 0; i < v->len; i++) {
            v->free_fn(vec_get(v, i));
        }
    }
    memset(v->buf, 0, v->len * v->type_size);
    v->len = 0;
    return;
}

void* vec_get(const Vec *v, size_t idx) {
    assert(v->type_size && "uninitialised vector");
    assert(idx < v->len && "index out of range");
    return v->buf + idx * v->type_size;
}

Str str_new(void) {
    Str s = {0};
    s.v.type_size = sizeof(char);
    s.char_pos.type_size = sizeof(size_t);
    s.offset_bytes = 0;
    return s;
}

Vec* str_as_vec(Str *s) {
    return &s->v;
}

static int build_character_table(Vec *ct, size_t start_off, const char *s, size_t size) {
    size_t i = start_off;
    while(i <= size) {
        int byte_count = utf8_byte_count(s[i]);
        if(byte_count < 1) return -1;
        size_t entry = i;
        vec_push(ct, &entry);
        if(s[i] == '\0') break;
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

    // remove null terminator
    vec_pop(&s->v, NULL);
    size_t original_size = s->v.len;
    vec_extend(&s->v, o, len);

    if(*VEC_GET(char, &s->v, s->v.len-1) != '\0') {
        char terminator = '\0';
        vec_push(&s->v, &terminator);
    }

    int ascii_only = s->char_pos.len == 0 && is_ascii(o, len);

    if(!ascii_only) {
        int start_off = 0;
        if(s->char_pos.len) {
            start_off = original_size;
        }
        // remove null terminator
        vec_pop(&s->char_pos, NULL);
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
    s->char_pos.len = 0;
    return;
}

void str_free(Str *s) {
    if(s->v.cap == SIZE_MAX) return;
    str_clear(s);
    vec_cleanup(&s->v);
    if(s->char_pos.buf) vec_cleanup(&s->char_pos);
}

// Truncates up to new_len
// Example:
//  `str_trunc("hello", 3) => "hel"`
void str_trunc(Str *s, size_t new_len) {
    assert(new_len <= str_len(s) && "index out of range");
    if(new_len == str_len(s)) return;
    if(new_len == 0) return str_clear(s);

    size_t new_last_idx = str_get_char_byte_idx(s, new_len);

    size_t new_len_idx = new_last_idx;

    s->v.len = new_len_idx+1;
    if(s->char_pos.len) {
        s->char_pos.len = new_len+1;
    }
    ((char*)s->v.buf)[new_last_idx] = '\0';

    return;
}

int str_insert_at(Str *s, size_t idx, const char *o, size_t len) {
    if(idx == str_len(s)) {
        return str_push(s, o, len);
    }

    vec_grow_to_fit(&s->v, str_size(s) + len);
    size_t byte_idx = str_get_char_byte_idx(s, idx);
    // move the overlapping part to the end of the line
    memmove(s->v.buf + byte_idx + len, s->v.buf + byte_idx, str_size(s) - byte_idx);
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
                str_size(s)
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

static int size_t_cmp(const void *a, const void *b) {
    return *(const size_t*)a - *(const size_t*)b;
}

// Returns the number of UTF-8 code points.
size_t str_len(const Str *s) {
    if(s->v.len == 0) return 0;
    if(s->char_pos.len) {
        size_t *off = bsearch(&s->offset_bytes,
                s->char_pos.buf,
                s->char_pos.len,
                s->char_pos.type_size,
                size_t_cmp
        );
        //return s->char_pos.len -1 - *off;
        return ((size_t*)s->char_pos.buf + s->char_pos.len -1) - off;
    }
    return s->v.len ? s->v.len -1 : 0;
}

int str_remove(Str *s, size_t start, size_t end) {
    assert(start <= end && "invalid range");

    utf32 end_char = 0;
    if(str_get_char(s, end, &end_char)) return -1;

    int end_char_width = utf32_len_utf8(end_char);

    size_t start_idx = str_get_char_byte_idx(s, start);
    size_t end_idx = str_get_char_byte_idx(s, end) + end_char_width;

    memmove(s->v.buf + start_idx, s->v.buf + end_idx, s->v.len - end_idx);

    size_t diff = end_idx - start_idx;
    s->v.len -= diff;
    ((char*)s->v.buf)[s->v.len-1] = '\0';

    if(s->char_pos.len) {
        vec_trunk(&s->char_pos, end);
        build_character_table(&s->char_pos, start_idx, s->v.buf, s->v.len);
    }

    return 0;
}

size_t str_get_char_byte_idx(const Str *s, size_t idx) {
    if(s->char_pos.len) {
        size_t *new_idx = VEC_GET(size_t, &s->char_pos, idx);
        // substract the offset from the head of the original slice if any
        if(new_idx) return *new_idx - s->offset_bytes;
        return -1;
    } else if (idx < s->v.len) return idx;
    return -1;
}

int str_get_char(const Str *s, size_t idx, utf32 *out) {
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
    if(!s) return new;

    size_t s_len = strlen(s);
    str_push(&new, s, s_len);
    return new;
}

// Equivalent to idx + <char*> on a cstr
const char* str_tail_cstr(const Str *s, size_t idx) {
    return str_as_cstr(s) + str_get_char_byte_idx(s, idx);
}

// Returns a readonly Str containing the chars after char idx
Str str_tail(const Str *s, size_t idx) {
    size_t byte_off = str_get_char_byte_idx(s, idx);
    Vec v = vec_tail(&s->v, byte_off);
    Vec char_pos = {0};
    if(!vec_is_empty(&s->char_pos)) {
        char_pos = vec_tail(&s->char_pos, idx);
    }
    return (Str) {
        .v = v,
        .char_pos = char_pos,
        .offset_bytes = byte_off + s->offset_bytes,
    };
}

Str str_head(const Str *s, size_t idx) {
    Vec v = s->v;
    v.cap = SIZE_MAX;

    Vec char_pos = s->char_pos;

    if(!vec_is_empty(&s->char_pos)) {
        char_pos.len = idx;
        char_pos.cap = SIZE_MAX;
    }

    if(idx == str_len(s)+1 || (str_len(s) == 0 && idx == 0)) {
        return (Str) {
            .v = v,
            .char_pos = char_pos,
        };
    }

    size_t byte_off = str_get_char_byte_idx(s, idx);
    if(byte_off == SIZE_MAX) {
        assert(0 && "Index out of range");
    }
    v.len = byte_off;

    return (Str) {
        .v = v,
        .char_pos = char_pos,
    };

}

Str str_from_cstr_len(const char *s, size_t len) {
    Str new = str_new();
    str_push(&new, s, len);
    return new;
}

int str_is_empty(Str *s) {
    return vec_is_empty(&s->v);
}

#ifdef TESTING

#include "tests.h"

TESTS_START

/*
TEST_DEF(test_str_len)
    Str s = str_from_cstr("ç");
    TEST_ASSERT(str_len(&s) == 1);
    str_free(&s);
TEST_ENDDEF

TEST_DEF(test_str_tail_len)
    Str s = str_from_cstr("hello world");
    TEST_ASSERT(str_len(&s) == 11);
    Str b = str_tail(&s, 6);
    TEST_ASSERT(str_len(&b) == 5);
    str_free(&s);
TEST_ENDDEF

TEST_DEF(test_str_len)
    Str s = str_from_cstr("ア");
    TEST_ASSERT(str_len(&s) == 1);
    str_free(&s);
TEST_ENDDEF
*/

TEST_DEF(test_str_len_tail)
    Str s = str_from_cstr("é");
    TEST_ASSERT(str_len(&s) == 1);

    Str tail = str_tail(&s, 1);
    TEST_ASSERT(str_len(&tail) == 0);
    str_free(&s);
TEST_ENDDEF

TESTS_END

#endif
