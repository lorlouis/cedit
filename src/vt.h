#ifndef VT_H
#define VT_H 1

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

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

int set_cursor_pos(uint16_t row, uint16_t col);

// prints to the main buffer of the terminal
// Returns
//  >=0 on success
//   -1 on success
int mprint(int fd, const char *restrict fmt, ...);

// Returns
//  >=0 on success
//   -1 on error
void alternate_buf_enter(void);

// Returns
//  >=0 on success
//   -1 on error
void alternate_buf_leave(void);

size_t count_cols(const char *restrict line, size_t line_len, int tab_width);

ssize_t take_cols(const char *restrict line, size_t line_len, size_t *nb_cols, int tab_width);

#endif
