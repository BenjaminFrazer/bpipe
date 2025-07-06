CC=gcc
CFLAGS=-I./bpipe -I./lib/Unity/src -std=c99 -Wall -Werror -pthread -save-temps=obj -g
LDFLAGS=-lm
SRC_DIR=bpipe
TEST_SRC_DIR=tests
BUILD_DIR=build
UNITY_SRC=lib/Unity/src/unity.c

# Find all test source files
TEST_SOURCES=$(wildcard $(TEST_SRC_DIR)/test_*.c)
# Generate test executable names from source files
TEST_EXECUTABLES=$(patsubst $(TEST_SRC_DIR)/%.c,$(BUILD_DIR)/%,$(TEST_SOURCES))
# Find all source files in bpipe directory
SRC_FILES=$(wildcard $(SRC_DIR)/*.c)
# Generate object files from source files
OBJ_FILES=$(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC_FILES))

.PHONY: all clean run test test-c test-py lint lint-c lint-py lint-fix clang-format-check clang-format-fix clang-tidy-check cppcheck-check ruff-check ruff-format-check ruff-fix

all: | $(BUILD_DIR)
all: $(TEST_EXECUTABLES)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(TEST_SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/unity.o: $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Generic rule for building test executables
# Each test depends on all object files from src dir, unity, and its own object file
$(BUILD_DIR)/test_%: $(BUILD_DIR)/test_%.o $(OBJ_FILES) $(BUILD_DIR)/unity.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)

run: all
	@echo "Run"

run-safe: all
	#./run_with_timeout.sh 30 ./$(BUILD_DIR)/test_core_filter

# Test targets
test: test-c test-py

test-c: all
	@echo "Running C tests..."
	@for test in $(TEST_EXECUTABLES); do \
		echo "Running $$test..."; \
		scripts/run_with_timeout.sh 1 $$test || exit 1; \
	done
	@echo "All C tests passed!"

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
	@clang-format -i $(SRC_DIR)/*.c $(SRC_DIR)/*.h

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

gdb:
	gdb --args python3-dbg demo_signal_to_plot.py
