# Makefile for running tests from the root directory

CC=gcc
CFLAGS=-I./bpipe -I./lib/Unity/src -std=c99 -Wall -Werror -pthread -save-temps=obj
LDFLAGS=-lm
SRC_DIR=bpipe
TEST_SRC_DIR=tests
BUILD_DIR=build
UNITY_SRC=lib/Unity/src/unity.c
TESTS=$(TEST_SRC_DIR)/test_core_filter.c $(TEST_SRC_DIR)/test_signal_gen.c $(TEST_SRC_DIR)/test_sentinel.c
OBJ_FILES=$(SRC_DIR)/core.c $(SRC_DIR)/signal_gen.c

.PHONY: all clean run

all: | $(BUILD_DIR)
all: $(BUILD_DIR)/test_core_filter $(BUILD_DIR)/test_signal_gen $(BUILD_DIR)/test_sentinel

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(TEST_SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/unity.o: $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_core_filter: $(BUILD_DIR)/core.o $(BUILD_DIR)/signal_gen.o $(BUILD_DIR)/unity.o $(BUILD_DIR)/test_core_filter.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/test_signal_gen: $(BUILD_DIR)/core.o $(BUILD_DIR)/signal_gen.o $(BUILD_DIR)/unity.o $(BUILD_DIR)/test_signal_gen.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/test_sentinel: $(BUILD_DIR)/core.o $(BUILD_DIR)/signal_gen.o $(BUILD_DIR)/unity.o $(BUILD_DIR)/test_sentinel.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)

run: all
	./$(BUILD_DIR)/test_core_filter
	./$(BUILD_DIR)/test_signal_gen
	./$(BUILD_DIR)/test_sentinel

hello:
	echo "Hello, Make output test"
