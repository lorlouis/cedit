#include "utf.h"
#include <limits.h>
#include "xalloc.h"

static int utf8_byte_count(utf8 c) {
    if((c & 0b11111000) == 0b11110000) return 4;
    if((c & 0b11110000) == 0b11100000) return 3;
    if((c & 0b11100000) == 0b11000000) return 2;
    if((c & 0b11000000) == 0b10000000) return 1;
    if((c & 0b11000000) == 0b00000000) return 1;
    return -1;
}

int utf32_len_utf8(utf32 c) {
    switch(c) {
        case 0x000000 ... 0x00007f:
            return 1;
        case 0x000080 ... 0x0007ff:
            return 2;
        case 0x000800 ... 0x00ffff:
            return 3;
        case 0x010000 ... 0x10ffff:
            return 4;
        default:
            return -1;
    }
}

int utf16_len_utf8(utf16 *s, size_t len) {
    TODO(implement this);
}

int utf16_to_utf32(utf16 *s, size_t len, utf32 *out);

int utf8_to_utf32(utf8 *s, size_t len, utf32 *out) {
    if(!s || !len) return 0;

    int byte_count = utf8_byte_count(*s);
    if(byte_count < 0 || len < (size_t)byte_count) return -1;
    utf32 code = 0;
    // extract the value present in the first byte
    switch(byte_count) {
        case 4:
            code |= (0b00000111 & *s);
            break;
        case 3:
            code |= (0b00001111 & *s);
            break;
        case 2:
            code |= (0b00011111 & *s);
            break;
        case 1:
            code |= (0b01111111 & *s);
            break;
    }
    // read the follow bytes
    for(size_t i = 1; i < (size_t)byte_count; i++) {
        code <<= 6;
        // check for invalid follow byte
        if(s[i] & 0b01000000) return -1;
        code |= (0b00111111 & s[i]);
    }
    *out = code;
    return byte_count;
}

int utf32_to_utf8(utf32 c, utf8 *buff, size_t size) {
    int byte_count = utf32_len_utf8(c);
    if(byte_count < 1) return -1;

}

int utf16_to_utf8(utf16 *s, size_t len, utf8 *buff, size_t size);

int utf8_to_utf32(utf8 *s, size_t len, utf32 *out);

wint_t utf32_to_wint(utf32 c);

utf32 wint_to_utf32(wint_t c);
