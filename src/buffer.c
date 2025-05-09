#include "buffer.h"
#include "str.h"
#include "line.h"
#include <stdint.h>
#include <errno.h>
#include "xalloc.h"
#include <math.h>
#include "commands.h"
#include "string.h"

FILE* filemode_open(
        enum FileMode fm,
        const char *path) {

    switch(fm) {
        case FM_RW: {
            // try to open the file
            FILE *f = fopen(path, "r+");
            // it's possible that the file does not exist
            if(!f && errno == ENOENT) {
                errno = 0;
                return 0;
            }
            return f;
        } break;
        case FM_RO:
            return fopen(path, "r");
            break;
    }
    return 0;
}


struct Buffer buffer_new(void) {
    struct Buffer buff = {0};
    buff.lines = VEC_NEW(struct Line, (void(*)(void*))line_free);
    return buff;
}

// Returns -1 on error and sets errno
int buffer_init_from_path(
        struct Buffer *buff,
        const char *path,
        enum FileMode fm) {

    errno = 0;
    FILE *f = filemode_open(fm, path);
    // there was an error
    if(!f && errno) return -1;

    *buff = buffer_new();

    // the file exists
    if(f) {
        char *line=0;
        size_t cap=0;
        ssize_t len=0;
        errno = 0;
        while((len=getline(&line, &cap, f)) > 0) {

            if(line[len-1] == '\n') {
                line[len-1] = '\0';
                len-=1;
            }

            struct Line l = line_from_cstr(line);
            vec_push(&buff->lines, &l);
        }
        xfree(line);

        if(len == -1 && ferror(f)) {
            errno = ferror(f);
            goto err;
        }
        fclose(f);
    }

    buff->in.ty = INPUT_FILE;
    buff->in.u.file.fm = fm;
    buff->in.u.file.path = str_from_cstr(path);

    buff->rc = 0;
    buff->fm = fm;

    return 0;

    err:
        vec_cleanup(&buff->lines);
        fclose(f);
        return -1;
}

/// Width of the number line for that given buffer
int buffer_num_width(struct Buffer *buff) {
    int num_width = ceil(log10((double)buff->lines.len)) + 1;
    if(num_width < 2) num_width = 2;
    return num_width;
}

struct Line *buffer_line_get(struct Buffer *buff, size_t idx) {
    buff->lines.type_size = sizeof(struct Line);
    if(idx == buff->lines.len) {
        struct Line new_line = line_new();
        vec_push(&buff->lines, &new_line);
    }
    return VEC_GET(struct Line, &buff->lines, idx);
}

void buffer_line_remove(struct Buffer *buff, size_t idx) {
    vec_remove(&buff->lines, idx);
}

int buffer_line_insert(struct Buffer *buff, size_t idx, struct Line line) {
    buff->lines.type_size = sizeof(struct Line);
    return vec_insert(&buff->lines, idx, &line);
}

// Returns -1 on error and sets errno
int buffer_dump(
        struct Buffer *buff,
        char *path) {

    if(buff->fm == FM_RO) {
        errno = EINVAL;
        return -1;
    }

    FILE *f = fopen(path, "w");
    if(!f) {
        return -1;
    }

    for(size_t i = 0; i < buff->lines.len; i++) {
        struct Line *line = buffer_line_get(buff, i);
        int ret = fprintf(f, "%.*s\n", (int)str_cstr_len(&line->text), str_as_cstr(&line->text));
        if(ret < 0) {
            errno = ferror(f);
            fclose(f);
            return -1;
        }
    }

    fclose(f);
    buff->dirty = 0;
    exec_command(str_as_cstr(&buff->onsave));
    return 0;
}

// DO NOT CALL DIRECTLY, call `re_state_rc_dec`
static void re_state_free(struct ReState *re_state) {
    if(re_state) {
        if(re_state->regex) {
            regfree(re_state->regex);
            xfree(re_state->regex);
            re_state->regex = 0;
        }
        vec_cleanup(&re_state->matches);
        if(re_state->error_str) {
            xfree(re_state->error_str);
            re_state->error_str = 0;
        }
    }
}

void re_state_clear_matches(struct ReState *re_state) {
    vec_clear(&re_state->matches);
}

void re_state_reset(struct ReState *re_state) {
    if(re_state->regex) {
        regfree(re_state->regex);
        memset(re_state->regex, 0, sizeof(regex_t));
        re_state_clear_matches(re_state);
    } else {
        re_state->regex = xcalloc(1, sizeof(regex_t));
        re_state->matches = VEC_NEW(struct ReMatch, 0);
        re_state_clear_matches(re_state);
    }
    if(re_state->error_str) {
        xfree(re_state->error_str);
        re_state->error_str = 0;
    }
}


// DO NOT USE DIRECTLY, USE `buffer_rc_dec`
static void buffer_cleanup(struct Buffer *buff) {
    vec_cleanup(&buff->lines);
    switch(buff->in.ty) {
        case INPUT_SCRATCH:
            break;
        case INPUT_FILE:
            str_free(&buff->in.u.file.path);
            break;
    }
    re_state_free(&buff->re_state);
    str_free(&buff->onsave);
    memset(buff, 0, sizeof(struct Buffer));
}


struct Buffer* buffer_rc_inc(struct Buffer *buff) {
    buff->rc += 1;
    return buff;
}

int buffer_rc_dec(struct Buffer *buff) {
    if(!buff->rc) {
        buffer_cleanup(buff);
        xfree(buff);
        return 1;
    }
    buff->rc -= 1;
    return 0;
}
