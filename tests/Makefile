CC = gcc
CFLAGS = -I../ -std=c99 -Wall -Werror -pthread
LDFLAGS = -pthread

SRC = ../bpipe/core.c
TEST = test_core_filter.c

all: test_core_filter

test_core_filter: $(SRC) $(TEST)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

run: test_core_filter
	./test_core_filter

clean:
	rm -f test_core_filter
