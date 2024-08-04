#ifndef LINE_H
#define LINE_H 1

#include "str.h"

struct Line {
    Str text;
    size_t render_width;
    // `Vec` of `uint8_t`
    Vec style_ids;
};

struct Line line_new(void);

struct Line line_from_cstr(char *s);

void line_free(struct Line *l);

int line_insert_at(struct Line *l, size_t idx, const char *s, size_t len);

void line_clear(struct Line *l);

void line_trunc(struct Line *l, size_t idx);

// Returns the borrowed head of l
struct Line line_head(struct Line *l, size_t idx);

// Returns the borrowed tail of l
struct Line line_tail(struct Line *l, size_t idx);

int line_remove(struct Line *l, size_t start, size_t end);

int line_append(struct Line *l, const char *s, size_t len);

#endif

