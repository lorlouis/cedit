#ifndef BUFFER_H
#define BUFFER_H 1

#include <regex.h>
#include <stdio.h>
#include "str.h"
#include "maybe.h"

enum FileMode {
    FM_RW = 0,
    FM_RO,
};

FILE* filemode_open(enum FileMode fm, const char *path);

struct Input {
    enum {
        INPUT_SCRATCH,
        INPUT_FILE,
    } ty;
    union {
        // zero sized as to not waste space
        int scratch[0];
        struct {
            Str path;
            enum FileMode fm;
        } file;
    } u;
};

typedef struct ViewCursor {
    size_t off_x;
    size_t off_y;
    size_t target_x;
} ViewCursor;


struct ReMatch {
    size_t line;
    size_t col;
    size_t len;
};

struct ReState {
    struct ViewCursor original_cursor;
    regex_t *regex;
    // `Vec` of `ReMatch`
    Vec matches;
    char *error_str;
};

void re_state_clear_matches(struct ReState *re_state);

void re_state_clear_matches(struct ReState *re_state);

void re_state_reset(struct ReState *re_state);

// Null initialise {0} to get a scratch buffer
struct Buffer {
    struct Input in;
    enum FileMode fm;
    // vec of lines
    Vec lines;
    int dirty;
    struct ReState re_state;
    size_t rc;
    Str onsave;
};

// Returns:
//  Non zero on success
//  0 on error and sets `errno`
FILE* filemode_open(
        enum FileMode fm,
        const char *path);

// Returns:
//  0 on success
//  -1 on error and sets `errno`
int filemode_save(
        enum FileMode fm,
        const char *path,
        struct Buffer *buff);


// increases the count and returns the data
// Returns
//  0 if the content does not exist
// >0 if the content exists
struct Buffer* buffer_rc_inc(struct Buffer *buff);

// decreases the count and frees if the count is 0
// Returns:
//  1 when the content of Rc got freed
//  0 when the content still exists
int buffer_rc_dec(struct Buffer *buff);

struct Line *buffer_line_get(struct Buffer *buff, size_t idx);

int buffer_line_insert(struct Buffer *buff, size_t idx, struct Line line);

struct Buffer buffer_new(void);

// Returns:
//  0 on success
//  -1 on error and sets `errno`
int buffer_init_from_path(
        struct Buffer *buff,
        const char *path,
        enum FileMode fm);

void buffer_line_remove(struct Buffer *buff, size_t idx);

// Returns -1 on error and sets errno
int buffer_dump(
        struct Buffer *buff,
        char *path);

#endif

