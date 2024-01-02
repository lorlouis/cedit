#ifndef TERMKEY_H
#define TERMKEY_H 1
#include "stddef.h"
#include "vt.h"
#include "utf.h"

enum KeyCode {
    KC_DEL = 127,
    KC_HOME = 256,
    KC_INS,
    KC_END,
    KC_PGUP,
    KC_PGDN,
    KC_F0,
    KC_F1,
    KC_F2,
    KC_F3,
    KC_F4,
    KC_F5,
    KC_F6,
    KC_F7,
    KC_F8,
    KC_F9,
    KC_F10,
    KC_F11,
    KC_F12,
    KC_F13,
    KC_F14,
    KC_F15,
    KC_F16,
    KC_F17,
    KC_F18,
    KC_F19,
    KC_F20,
    KC_ARRUP,
    KC_ARRDOWN,
    KC_ARRRIGHT,
    KC_ARRLEFT,
};

enum KeyModifier {
    KM_Shift = 1,
    KM_Alt = 2,
    KM_Ctrl = 4,
    KM_Meta = 8,
};

struct KeyEvent {
    // bitset of `KeyModifier values`
    int modifier;
    utf32 key;
};

// Returns
//  1 if the keys are equal
//  0 otherwise
int cmp_keys(
        struct KeyEvent *restrict e,
        int modifiers,
        enum KeyCode k);

// Assumes fd does not block on read
// Returns
//  1 on success
//  0 when no keys are present
//  -1 on io error, check errno
//  -2 on invalid input
int readkey(int fd, struct KeyEvent *restrict e);

// Tries to format the key even into buffer
// can be called on a Null buffer to get the size it would take to print
//
// Returns
// >= 0 on success (number of bytes written excluding the null terminator)
// -1 if buffer is too small
int keyevent_fmt(struct KeyEvent *e, char *buff, size_t len);

wchar_t keyevent_to_wchar(struct KeyEvent *e);

int check_utf8_start(char c);

#endif

