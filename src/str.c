#include <string.h>
#include "xalloc.h"

#include "str.h"

void str_push(Str *s, char *o, size_t len) {
    if(s->cap <= s->len + len + 1) {
        size_t new_cap = s->cap ? s->cap * 2 : 2;
        s->buf = xrealloc(s->buf, new_cap);
        s->cap = new_cap;
    }
    if((void*)o >= (void*)s && (void*)o <= (void*)s + s->len) {
        memmove(s->buf+s->len, o, len);
    } else {
        memcpy(s->buf+s->len, o, len);
    }
    s->len += len;
    return;
}

void str_clear(Str *s) {
    if(s->buf && s->cap >= 1) {
        s->buf[0] = '\0';
    }
    s->len = 0;
    return;
}

void str_trunc(Str *s, size_t new_len) {
    if(new_len >= s->len) return;
    s->len = new_len;
    s->buf[new_len+1] = '\0';
    return;
}
