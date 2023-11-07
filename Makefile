ENTRYPOINT	= main.c
SOURCE	= vt.c editor.c termkey.c xalloc.c
TEST_SOURCE	= test.c
HEADER	=
SRC_DIR = src
BUILD_DIR = build
TEST_DIR = tests
OUT	= a.out
CC	= gcc
FLAGS	= --std=gnu17 -g -Wall -Wextra
LFLAGS	=

ENTRYPOINT_OBJ = $(patsubst %.c,$(BUILD_DIR)/%.o,$(ENTRYPOINT))

OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCE))

TEST_OBJS = $(patsubst %.c,$(BUILD_DIR)/$(TEST_DIR)_%.o,$(TEST_SOURCE))

all: compile test

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

clean:
	rm -f $(OBJS) $(OUT) $(ENTRYPOINT_OBJ) $(TEST_OBJS) test_$(OUT)
	rmdir $(BUILD_DIR)

