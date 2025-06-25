# Makefile for running tests from the root directory

CC=gcc
CFLAGS=-I./bpipe -std=c99 -Wall -Werror -pthread -save-temps
LDFLAGS=-lm
SRC_DIR=bpipe
TEST_SRC_DIR=tests
TESTS=$(TEST_SRC_DIR)/test_core_filter.c $(TEST_SRC_DIR)/test_signal_gen.c $(TEST_SRC_DIR)/test_sentinel.c
OBJ_FILES=$(SRC_DIR)/core.c $(SRC_DIR)/signal_gen.c

.PHONY: all clean run

all: $(TESTS:.c=.o) test_core_filter test_signal_gen test_sentinel

test_core_filter: $(OBJ_FILES) $(TEST_SRC_DIR)/test_core_filter.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_signal_gen: $(OBJ_FILES) $(TEST_SRC_DIR)/test_signal_gen.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_sentinel: $(OBJ_FILES) $(TEST_SRC_DIR)/test_sentinel.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TEST_SRC_DIR)/*.o test_core_filter test_signal_gen test_sentinel

run: all
	./test_core_filter
	./test_signal_gen
	./test_sentinel

hello:
	echo "Hello, Make output test"
