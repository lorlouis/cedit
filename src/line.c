#include "line.h"

#include <stdint.h>
#include "vt.h"

struct Line line_new(void) {
    struct Line l = {0};
    l.text = str_new();
    l.style_ids = VEC_NEW(uint8_t, 0);
    return l;
}

struct Line line_from_cstr(char *s) {
    Str str = str_from_cstr(s);
    struct Line line = line_new();
    line.render_width = render_width(&str, str_len(&str));
    line.text = str;
    return line;
}

void line_free(struct Line *l) {
    str_free(&l->text);
    vec_cleanup(&l->style_ids);
}

int line_insert_at(struct Line *l, size_t idx, const char *s, size_t len) {
    int ret = str_insert_at(&l->text, idx, s, len);
    l->render_width = render_width(&l->text, str_len(&l->text));
    return ret;
}

void line_clear(struct Line *l) {
    l->render_width = 0;
    str_clear(&l->text);
}

void line_trunc(struct Line *l, size_t idx) {
    Str tail = str_tail(&l->text, idx);
    size_t trunked_width = render_width(&tail, str_len(&tail));
    str_trunc(&l->text, idx);
    l->render_width -= trunked_width;
}

struct Line line_head(struct Line *l, size_t idx) {
    Str head = str_head(&l->text, idx);
    size_t head_width = render_width(&head, str_len(&head));
    return (struct Line) {
        .text = head,
        .style_ids = l->style_ids,
        .render_width = head_width,
    };
}

struct Line line_tail(struct Line *l, size_t idx) {
    Str tail = str_tail(&l->text, idx);
    size_t tail_width = render_width(&tail, str_len(&tail));
    return (struct Line) {
        .text = tail,
        .style_ids = l->style_ids,
        .render_width = tail_width,
    };
}

int line_remove(struct Line *l, size_t start, size_t end) {
    int ret = str_remove(&l->text, start, end);
    l->render_width = render_width(&l->text, str_len(&l->text));
    return ret;
}

int line_append(struct Line *l, const char *s, size_t len) {
    int ret = str_push(&l->text, s, len);
    l->render_width = render_width(&l->text, str_len(&l->text));
    return ret;
}
