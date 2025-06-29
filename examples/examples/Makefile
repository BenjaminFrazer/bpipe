# Makefile for bpipe examples

CC=gcc
CFLAGS=-I../bpipe -std=c99 -Wall -Werror -pthread
LDFLAGS=-lm
SRC_DIR=../bpipe
BUILD_DIR=build

# Source files needed for examples
CORE_SRCS=$(SRC_DIR)/core.c $(SRC_DIR)/signal_gen.c $(SRC_DIR)/tee.c $(SRC_DIR)/resampler.c

# Example programs
EXAMPLES=multi_rate_sync

.PHONY: all clean

all: | $(BUILD_DIR)
all: $(EXAMPLES)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Link examples
multi_rate_sync: $(BUILD_DIR)/multi_rate_sync.o $(BUILD_DIR)/core.o $(BUILD_DIR)/signal_gen.o $(BUILD_DIR)/resampler.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR) $(EXAMPLES)