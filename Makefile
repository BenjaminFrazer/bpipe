# Makefile for running tests from the root directory

CC=gcc
CFLAGS=-I./bpipe -I./lib/Unity/src -std=c99 -Wall -Werror -pthread -save-temps=obj
LDFLAGS=-lm
SRC_DIR=bpipe
TEST_SRC_DIR=tests
BUILD_DIR=build
UNITY_SRC=lib/Unity/src/unity.c
TESTS=$(TEST_SRC_DIR)/test_core_filter.c $(TEST_SRC_DIR)/test_signal_gen.c $(TEST_SRC_DIR)/test_sentinel.c $(TEST_SRC_DIR)/test_simple_multi_output.c
OBJ_FILES=$(SRC_DIR)/core.c $(SRC_DIR)/signal_gen.c $(SRC_DIR)/tee.c

.PHONY: all clean run test test-c test-py lint lint-c lint-py lint-fix clang-format-check clang-format-fix clang-tidy-check cppcheck-check ruff-check ruff-format-check ruff-fix

all: | $(BUILD_DIR)
all: $(BUILD_DIR)/test_core_filter $(BUILD_DIR)/test_signal_gen $(BUILD_DIR)/test_sentinel $(BUILD_DIR)/test_simple_multi_output

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(TEST_SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/unity.o: $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_core_filter: $(BUILD_DIR)/core.o $(BUILD_DIR)/signal_gen.o $(BUILD_DIR)/tee.o $(BUILD_DIR)/unity.o $(BUILD_DIR)/test_core_filter.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/test_signal_gen: $(BUILD_DIR)/core.o $(BUILD_DIR)/signal_gen.o $(BUILD_DIR)/tee.o $(BUILD_DIR)/unity.o $(BUILD_DIR)/test_signal_gen.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/test_sentinel: $(BUILD_DIR)/core.o $(BUILD_DIR)/signal_gen.o $(BUILD_DIR)/tee.o $(BUILD_DIR)/unity.o $(BUILD_DIR)/test_sentinel.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/test_simple_multi_output: $(BUILD_DIR)/core.o $(BUILD_DIR)/signal_gen.o $(BUILD_DIR)/tee.o $(BUILD_DIR)/unity.o $(BUILD_DIR)/test_simple_multi_output.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)

run: all
	./$(BUILD_DIR)/test_core_filter
	./$(BUILD_DIR)/test_signal_gen
	./$(BUILD_DIR)/test_sentinel
	./$(BUILD_DIR)/test_simple_multi_output

run-safe: all
	./run_with_timeout.sh 30 ./$(BUILD_DIR)/test_core_filter
	./run_with_timeout.sh 30 ./$(BUILD_DIR)/test_signal_gen
	./run_with_timeout.sh 30 ./$(BUILD_DIR)/test_sentinel
	./run_with_timeout.sh 30 ./$(BUILD_DIR)/test_simple_multi_output

# Test targets
test: test-c test-py

test-c: all
	@echo "Running C tests..."
	./$(BUILD_DIR)/test_core_filter
	./$(BUILD_DIR)/test_signal_gen
	./$(BUILD_DIR)/test_sentinel
	./$(BUILD_DIR)/test_simple_multi_output

test-py: build-py
	@echo "Running Python tests..."
	python -m pytest py-tests -v

test-py-coverage: build-py
	@echo "Running Python tests with coverage..."
	python -m pytest py-tests --cov=bpipe --cov-report=html --cov-report=term

build-py:
	@echo "Building Python extension..."
	python setup.py build_ext --inplace

# Linting targets
lint: lint-c lint-py

lint-c: clang-format-check clang-tidy-check cppcheck-check

lint-py: ruff-check ruff-format-check

lint-fix: clang-format-fix ruff-fix

# C linting targets
clang-format-check:
	@echo "Checking C code formatting..."
	@clang-format --dry-run --Werror $(SRC_DIR)/*.c $(SRC_DIR)/*.h tests/*.c

clang-format-fix:
	@echo "Fixing C code formatting..."
	@clang-format -i $(SRC_DIR)/*.c $(SRC_DIR)/*.h tests/*.c

clang-tidy-check:
	@echo "Running clang-tidy static analysis..."
	@clang-tidy $(SRC_DIR)/*.c -- $(CFLAGS)

cppcheck-check:
	@echo "Running cppcheck static analysis..."
	@cppcheck --enable=all --suppress=missingIncludeSystem --suppress=missingReturn --suppress=unusedFunction --suppress=truncLongCastReturn --error-exitcode=1 $(SRC_DIR)/

# Python linting targets
ruff-check:
	@echo "Running ruff linting..."
	@python -m ruff check .

ruff-format-check:
	@echo "Checking Python code formatting..."
	@python -m ruff format --check .

ruff-fix:
	@echo "Fixing Python code issues..."
	@python -m ruff check --fix .
	@python -m ruff format .

hello:
	echo "Hello, Make output test"
