#ifndef VT_H
#define VT_H 1

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <stdbool.h>

#include "utf.h"
#include "str.h"

#define ESC "\e"
#define CSI ESC "["
#define ST  ESC "\\"
#define RESET CSI "0m"

#define BUF_ALT CSI "?1049h"
#define BUF_MAIN CSI "?1049l"

// clear screen
#define CLS CSI "2J"

#define CUR_HIDE CSI "?25l"
#define CUR_SHOW CSI "?25h"

#define INV CSI "7m"

#define URL(path, text) ESC "]8;;" path ST text ESC "]8;;" ST

extern volatile int IN_ALTERNATE_BUF;

extern volatile int CUSOR_HIDDEN;

enum VtColour {
    VT_BLK = 0,
    VT_RED = 1,
    VT_GRN = 2,
    VT_YEL = 3,
    VT_BLU = 4,
    VT_MAG = 5,
    VT_CYA = 6,
    VT_WHI = 7,
    VT_GRA,
    VT_BR_RED,
    VT_BR_GRN,
    VT_BR_YEL,
    VT_BR_BLU,
    VT_BR_MAG,
    VT_BR_CYA,
    VT_BR_WHI,
};

enum ColourType {
    COL_NONE = 0,
    COL_VT,
    COL_RGB,
};

typedef struct {
    enum ColourType t;
    union {
        enum VtColour vt;
        struct {
            uint8_t r;
            uint8_t g;
            uint8_t b;
        } rgb;
    };
} Colour;

Colour colour_none();

Colour colour_vt(enum VtColour vt);

Colour colour_rgb(uint8_t r, uint8_t g, uint8_t b);

typedef struct {
    Colour fg;
    Colour bg;
    Colour underline_color;
    enum FontWeight {
        WEIGHT_NORMAL = 0,
        WEIGHT_BOLD,
        WEIGHT_FAINT,
    } weight;
    bool inverted;
    enum UnderlineStyle {
        UNDERLINE_NONE = 0,
        UNDERLINE_SIMPLE,
        UNDERLINE_DOUBLE,
    } underline;
} Style;

int style_begin(const Style *s, int fd);

int style_reset(int fd);

Style style_new();

Style style_fg(Style s, const Colour c);

Style style_bg(Style s, const Colour c);

Style style_underline_color(Style s, const Colour c);

Style style_weight(Style s, enum FontWeight weight);

Style style_inverted(Style s, bool inverted);

Style style_underline(Style s, enum UnderlineStyle underline);

int style_fmt(const Style *s, int fd, const char *fmt, ...);

int set_cursor_pos(uint16_t row, uint16_t col);

// Returns
//  >=0 on success
//   -1 on error
void alternate_buf_enter(void);

// Returns
//  >=0 on success
//   -1 on error
void alternate_buf_leave(void);

size_t count_cols(const Str *restrict line, int tab_width);

ssize_t take_cols(const Str *restrict line, size_t *nb_cols, int tab_width);

#endif
