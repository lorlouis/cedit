#include "highlight.h"

#include "xalloc.h"

#include <string.h>

size_t CURRENT_ID = 0;

struct StyleEntry {
    char *name;
    Style style;
};

struct StyleEntry STYLE_ENTRY_TABLE[255] = {0};

void style_entry_free(struct StyleEntry *entry) {
    if(entry->name) {
        xfree(entry->name);
    }
}

int style_find_id(char *name) {
    for(size_t i = 0; i < 255; i++) {
        char *entry_name = STYLE_ENTRY_TABLE[i].name;
        if(entry_name && !strcmp(name, entry_name)) {
            return i;
        }
    }
    return -1;
}

int find_next_free_after(uint8_t target_id) {
    for(size_t i = target_id; i < 255; i++) {
        if(!STYLE_ENTRY_TABLE[i].name) return i;
    }
    return -1;
}

// tries to register a style, already defined styles have priority
// Returns:
//   < 0 On error (out of space after priority)
//  >= 0 On success (style id)
int style_register(char *name, size_t name_len, Style style, uint8_t priority) {
    uint8_t target_id = 255 - priority;
    int id = find_next_free_after(target_id);
    if(id < 0) return id;

    struct StyleEntry *entry = &STYLE_ENTRY_TABLE[id];

    entry->name = xcalloc(name_len, sizeof(char) + 1);

    memcpy(entry->name, name, name_len);

    entry->style = style;

    return id;
}

Style* style_find_by_id(uint8_t id) {
    if(!STYLE_ENTRY_TABLE[id].name) return 0;
    return &STYLE_ENTRY_TABLE[id].style;
}

Style* style_find(char *name) {
    int id = style_find_id(name);
    if(id < 0) return 0;
    return &STYLE_ENTRY_TABLE[id].style;
}

int style_delist_by_id(uint8_t id) {
    if(!STYLE_ENTRY_TABLE[id].name) return -1;

    xfree(STYLE_ENTRY_TABLE[id].name);
    STYLE_ENTRY_TABLE[id].name = 0;

    return 0;
}

int style_delist(char *name) {
    int id = style_find_id(name);
    if(id < 0) return -1;
    return style_delist_by_id(id);
}

void style_entry_table_free(void) {
    for(size_t i = 0; i < 255; i++) {
        style_entry_free(&STYLE_ENTRY_TABLE[i]);
    }
}

#ifdef TESTING

#include "tests.h"

TESTS_START

TESTS_END

#endif
