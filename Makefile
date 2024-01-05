ENTRYPOINT	= main.c
SOURCE	= vt.c editor.c termkey.c xalloc.c str.c utf.c commands.c config.c highlight.c
TEST_SOURCE	= test.c
HEADER	=
SRC_DIR = src
BUILD_DIR = build
TEST_DIR = tests
OUT	= a.out
CC	?= gcc
EXTRAFLAGS ?=
FLAGS	= --std=gnu17 -g -Wall -Wextra $(EXTRAFLAGS)
LFLAGS	= -lm

ENTRYPOINT_OBJ = $(patsubst %.c,$(BUILD_DIR)/%.o,$(ENTRYPOINT))

OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCE))

TEST_OBJS = $(patsubst %.c,$(BUILD_DIR)/$(TEST_DIR)_%.o,$(TEST_SOURCE))

all: compile test

compile_commands.json:
	bear -- make

.PHONY: doc
doc: compile_commands.json
	clang-doc --executor=all-TUs --format=html compile_commands.json --project-name $(OUT) --doxygen

compile: $(ENTRYPOINT_OBJ) $(OBJS)
	$(CC) -o $(OUT) $^ $(LFLAGS)

test: $(TEST_OBJS) $(OBJS)
	$(CC) -o test_$(OUT) $^ $(LFLAGS)
	./test_$(OUT)

$(BUILD_DIR):
	mkdir $(BUILD_DIR)

$(BUILD_DIR)/$(TEST_DIR)_%.o: $(TEST_DIR)/%.c $(BUILD_DIR)
	$(CC) -I$(SRC_DIR) $(FLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(BUILD_DIR)
	$(CC) $(FLAGS) -c -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJS) $(OUT) $(ENTRYPOINT_OBJ) $(TEST_OBJS) test_$(OUT) compile_commands.json
	rm -rf docs
	rmdir $(BUILD_DIR) 2>/dev/null || true
