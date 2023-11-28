#ifndef UTF_H
#define UTF_H 1

#include <stdint.h>
#include <wctype.h>
#include <stddef.h>

typedef uint32_t utf32;
typedef char utf8;

// needs to be called at startup
int load_locale(void);

int utf8_byte_count(utf8 c);

int utf32_len_utf8(utf32 c);

int utf8_to_utf32(const utf8 *s, size_t len, utf32 *out);

int utf32_to_utf8(utf32 c, utf8 *buff, size_t size);

int utf8_to_utf32(const utf8 *s, size_t len, utf32 *out);

wint_t utf32_to_wint(utf32 c);

utf32 wint_to_utf32(wint_t c);

#endif

