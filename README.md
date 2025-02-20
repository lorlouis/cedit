# CEDIT (Name subject to change)

Name suggestions are welcome

A small utf-8 text editor with minimal dependencies
(only supports LTR languages sadly)

[![A demo of the editor in action](https://img.youtube.com/vi/HsTKXVJA5yw/0.jpg)](https://www.youtube.com/watch?v=HsTKXVJA5yw)

## Status

This project is not actively maintained; do not use it for anything serious.

(do not edit `/etc/fstab` with it for example)

## How to build

Simply run `make` and an `a.out` file will be generated.

## Testing

This project uses it's own unit test framework, examples of unit tests can be
found at the bottom of .c files throughout the codebase.

### fanalyzer

When build with GCC it is recommended to build the project with
`EXTRAFLAGS=-fanalyzer make`. This will help catch some bugs at compile time.

### TODO

- [ ] Add proper syntax highlight, most likely regex based but tree-sitter could
    be interesting.

- [ ] Add proper key-sequence parser and state machine with timeout to allow
    commands such as `gg` or `gd`

- [ ] Make it so that unit tests fork so that segfaults
    can be caught by the test framework

- [ ] Add proper support for block copy

- [ ] Add support for char offset so that long wrapped lines can be scrolled
    smoothly.

#### Not sure I'll ever do them

- [ ] Switch utf.* to libgraphemes

- [ ] add support for a basic lsp client (requires utf8<=>utf16 conversions)

- [ ] Find a better name than cedit

- [ ] Switch off Array-of-strings as the main data structure to hold text
    (ropes are an easy choice, but piece tables would probably fit this
    project better)
