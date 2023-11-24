#ifndef UTF_H
#define UTF_H 1

#include <stdint.h>
#include <wchar.h>

typedef uint32_t utf32;
typedef uint16_t utf16;
typedef uint8_t utf8;

int utf32_len_utf8(utf32 c);

int utf16_len_utf8(utf16* s, size_t len);

int utf16_to_utf32(utf16 *s, size_t len, utf32 *out);

int utf8_to_utf32(utf8 *s, size_t len, utf32 *out);

int utf32_to_utf8(utf32 c, utf8 *buff, size_t size);

int utf16_to_utf8(utf16 *s, size_t len, utf8 *buff, size_t size);

int utf8_to_utf32(utf8 *s, size_t len, utf32 *out);

wint_t utf32_to_wint(utf32 c);

utf32 wint_to_utf32(wint_t c);

#endif

