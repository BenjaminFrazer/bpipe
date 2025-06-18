CC = gcc
CFLAGS = -I../ -std=c99 -Wall -Werror -pthread
LDFLAGS = -pthread -lm

SRC = ../bpipe/core.c ../bpipe/signal_gen.c

all: test_core_filter test_signal_gen

# existing test
TEST_CORE = test_core_filter.c

test_core_filter: $(SRC) $(TEST_CORE)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# new generator test
TEST_GEN = test_signal_gen.c

test_signal_gen: $(SRC) $(TEST_GEN)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

run_core: test_core_filter
	./test_core_filter

run_gen: test_signal_gen
	./test_signal_gen

clean:
	rm -f test_core_filter test_signal_gen
