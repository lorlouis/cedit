#ifndef COLOURSCHEME_H
#define COLOURSCHEME_H 1

#include "vt.h"
#include <stdint.h>

#define SEARCH_HIGHLIGHT "search_highlight"

// tries to register a style, already defined styles have priority
// Returns:
//   < 0 On error (out of space after priority)
//  >= 0 On success (style id)
int style_register(char *name, size_t name_len, Style style, uint8_t priority);

int style_find_id(char *name);

Style* style_find(char *name);

Style* style_find_by_id(uint8_t id);

int style_delist(char *name);

int style_delist_by_id(uint8_t id);

#endif

