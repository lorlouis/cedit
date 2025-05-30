ENTRYPOINT	= main.c
SOURCE	= vt.c editor.c termkey.c xalloc.c str.c utf.c commands.c config.c highlight.c exec.c line.c buffer.c linkedlist.c
HEADER	=
SRC_DIR = src
BUILD_DIR = build
TEST_DIR = tests
OUT	= a.out
CC	?= gcc
EXTRAFLAGS ?=
CFLAGS	= --std=gnu23 -g -Wall -Wextra $(EXTRAFLAGS) -I$(SRC_DIR) -fanalyzer -Wno-analyzer-use-of-uninitialized-value -fsanitize=bounds-strict,undefined#,address
TEST_FLAGS = $(CFLAGS) -DTESTING=1 -Itests
LFLAGS	= -lm -lubsan # -lasan
TEST_LFLAGS = $(LFLAGS)


ENTRYPOINT_OBJ = $(patsubst %.c,$(BUILD_DIR)/%.o,$(ENTRYPOINT))

OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCE))
TESTABLE_SOURCES != grep -rl '\#ifdef TESTING' $(SRC_DIR) \
	| sed "s$(SRC_DIR)/" \
	| sed "s$(ENTRYPOINT)" \
	| tr '\n' ' '
TEST_OBJS = $(patsubst %.c,$(BUILD_DIR)/$(TEST_DIR)_%.o,$(TESTABLE_SOURCES))
TEST_EXECS = $(patsubst %.c,$(BUILD_DIR)/$(TEST_DIR)_%,$(TESTABLE_SOURCES))

all: $(BUILD_DIR) compile tests

compile_commands.json:
	bear -- make

.PHONY: doc
doc: compile_commands.json
	cldoc generate $(CFLAGS) -isysroot

compile: $(ENTRYPOINT_OBJ) $(OBJS)
	$(CC) -o $(OUT) $^ $(LFLAGS)

.PHONY: test
test: tests
.PHONY: tests
tests: $(TEST_EXECS)
	@[[ -z "$^" ]] \
		&& echo "no tests to run" \
		|| echo $^ | xargs -n 1 bash -c

$(BUILD_DIR):
	mkdir $(BUILD_DIR)

$(BUILD_DIR)/$(TEST_DIR)_%.o: $(SRC_DIR)/%.c $(BUILD_DIR)
	$(CC) $(TEST_FLAGS) -c -o $@ $<

$(BUILD_DIR)/$(TEST_DIR)_%: $(BUILD_DIR)/$(TEST_DIR)_%.o $(OBJS)
	$(CC) -o $@ $(filter-out $(patsubst $(BUILD_DIR)/$(TEST_DIR)_%.o,$(BUILD_DIR)/%.o,$<),$^) $(LFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJS) $(OUT) $(ENTRYPOINT_OBJ) $(TEST_OBJS) test_$(OUT) $(TEST_EXECS) compile_commands.json
	rmdir $(BUILD_DIR) 2>/dev/null || true
