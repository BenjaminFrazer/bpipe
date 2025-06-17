CC = gcc
CFLAGS = -I../ -I../external/unity -std=c99 -Wall -Werror $(shell python3-config --includes)
LDFLAGS = $(shell python3-config --ldflags) -lpython3.10

UNITY_ROOT=../lib/Unity/

SRC = ../bpipe/core.c $(UNITY_ROOT)/src/unity.c
TEST = test_core_filter.c

all: test_core_filter

test_core_filter: $(SRC) $(TEST)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

run: test_core_filter
	./test_core_filter

clean:
	rm -f test_core_filter
