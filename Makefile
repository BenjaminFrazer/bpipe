PROJECT_ROOT=./
CC=gcc
CFLAGS = -std=c99 -Wall -Werror -pthread -save-temps=obj -g 
CFLAGS += -I$(PROJECT_ROOT)/bpipe -I$(PROJECT_ROOT)/tests -I$(PROJECT_ROOT)/lib/Unity/src
CFLAGS += -DUNITY_OUTPUT_COLOR
#CFLAGS += -DUNITY_DEBUG_BREAK_ON_FAIL
LDFLAGS=-lm
SRC_DIR=bpipe
TEST_SRC_DIR=tests
EXAMPLES_DIR=examples
BUILD_DIR=build
UNITY_SRC=lib/Unity/src/unity.c
DEP_FLAGS = -MMD -MP

# Find all test source files
TEST_SOURCES=$(wildcard $(TEST_SRC_DIR)/test_*.c)
# Generate test executable names from source files
TEST_EXECUTABLES=$(patsubst $(TEST_SRC_DIR)/%.c,$(BUILD_DIR)/%,$(TEST_SOURCES))
# Add filter compliance test to the list
TEST_EXECUTABLES += $(BUILD_DIR)/test_filter_compliance
# Find all source files in bpipe directory
SRC_FILES=$(wildcard $(SRC_DIR)/*.c)
# Generate object files from source files
OBJ_FILES=$(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC_FILES))
# List of working examples (add more as they are fixed)
# To add a new example, just add its name (without .c extension) to this list
WORKING_EXAMPLES=csv_to_debug_auto csv_to_csv_scale
# Generate full paths for working examples
EXAMPLE_EXECUTABLES=$(addprefix $(EXAMPLES_DIR)/,$(WORKING_EXAMPLES))

.PHONY: all clean run test test-c test-py lint lint-c lint-py lint-fix clang-format-check clang-format-fix clang-tidy-check cppcheck-check ruff-check ruff-format-check ruff-fix examples compliance compliance-lifecycle compliance-dataflow compliance-buffer compliance-perf help-compliance

all: | $(BUILD_DIR)
all: $(TEST_EXECUTABLES) examples

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(DEP_FLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(TEST_SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(DEP_FLAGS) -c -o $@ $<

$(BUILD_DIR)/unity.o: $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(DEP_FLAGS) -c -o $@ $<

# Generic rule for building test executables
# Each test depends on all object files from src dir, unity, and its own object file
$(BUILD_DIR)/test_%: $(BUILD_DIR)/test_%.o $(OBJ_FILES) $(BUILD_DIR)/unity.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)


# Filter compliance test suite
FILTER_COMPLIANCE_DIR=tests/filter_compliance
FILTER_COMPLIANCE_SOURCES=$(wildcard $(FILTER_COMPLIANCE_DIR)/test_*.c)
FILTER_COMPLIANCE_OBJS=$(patsubst $(FILTER_COMPLIANCE_DIR)/%.c,$(BUILD_DIR)/filter_compliance_%.o,$(FILTER_COMPLIANCE_SOURCES))

# Build rule for filter compliance object files
$(BUILD_DIR)/filter_compliance_%.o: $(FILTER_COMPLIANCE_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(DEP_FLAGS) -c -o $@ $<

# Build the filter compliance test executable
$(BUILD_DIR)/test_filter_compliance: $(BUILD_DIR)/filter_compliance_main.o $(BUILD_DIR)/filter_compliance_common.o $(BUILD_DIR)/filter_compliance_compliance_matrix.o $(FILTER_COMPLIANCE_OBJS) $(BUILD_DIR)/mock_filters.o $(OBJ_FILES) $(BUILD_DIR)/unity.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Examples target
examples: $(EXAMPLE_EXECUTABLES)

# Generic rule for building example executables
# Adjust include paths for examples - support both "bpipe/header.h" and "header.h" styles
$(EXAMPLES_DIR)/%: $(EXAMPLES_DIR)/%.c $(OBJ_FILES)
	$(CC) -std=c99 -Wall -Werror -pthread -g -I$(PROJECT_ROOT) -I$(PROJECT_ROOT)/bpipe -I$(PROJECT_ROOT)/lib/Unity/src -o $@ $< $(OBJ_FILES) $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(EXAMPLE_EXECUTABLES)

# Help target for compliance testing
help-compliance:
	@echo "Filter Compliance Testing Targets:"
	@echo "  make compliance                    - Run all compliance tests for all filters"
	@echo "  make compliance FILTER=Passthrough - Run all tests for specific filter"
	@echo "  make compliance-lifecycle          - Run only lifecycle tests"
	@echo "  make compliance-dataflow           - Run only dataflow tests"
	@echo "  make compliance-buffer             - Run only buffer configuration tests"
	@echo "  make compliance-perf               - Run only performance tests"
	@echo ""
	@echo "All targets support the FILTER variable to test specific filters."
	@echo ""
	@echo "Examples:"
	@echo "  make compliance FILTER=Passthrough"
	@echo "  make compliance-buffer FILTER=ControllableConsumer"
	@echo "  make compliance-lifecycle    # Run lifecycle tests for all filters"
	@echo ""
	@echo "The test executable also supports:"
	@echo "  ./build/test_filter_compliance --filter <name> --test <pattern>"

project_root:
	echo $(PROJECT_ROOT)

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
		scripts/run_with_timeout.sh 4 $$test || exit 1; \
	done
	@echo "All C tests passed!"

test-c-quiet: all
	@echo "Running C tests (quiet mode)..."
	@for test in $(TEST_EXECUTABLES); do \
		scripts/run_test_quiet.sh 4 $$test || exit 1; \
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

# Individual test targets
test-csv-sink: $(BUILD_DIR)/test_csv_sink
	@echo "Running CSV sink tests..."
	scripts/run_with_timeout.sh 30 $(BUILD_DIR)/test_csv_sink

test-debug-output: $(BUILD_DIR)/test_debug_output_filter
	@echo "Running debug output filter tests..."
	scripts/run_with_timeout.sh 30 $(BUILD_DIR)/test_debug_output_filter

test-filter-compliance: $(BUILD_DIR)/test_filter_compliance
	@echo "Running filter compliance tests..."
	scripts/run_with_timeout.sh 60 $(BUILD_DIR)/test_filter_compliance

# Run compliance tests with optional filter pattern
# Usage: make compliance FILTER=Passthrough
# Usage: make compliance (runs all filters)
compliance: $(BUILD_DIR)/test_filter_compliance
	@echo "Running filter compliance tests..."
	@if [ -n "$(FILTER)" ]; then \
		echo "Testing filter: $(FILTER)"; \
		scripts/run_with_timeout.sh 60 $(BUILD_DIR)/test_filter_compliance --filter $(FILTER); \
	else \
		echo "Testing all filters"; \
		scripts/run_with_timeout.sh 60 $(BUILD_DIR)/test_filter_compliance; \
	fi

# Run specific compliance test categories
compliance-lifecycle: $(BUILD_DIR)/test_filter_compliance
	@echo "Running lifecycle compliance tests..."
	@if [ -n "$(FILTER)" ]; then \
		scripts/run_with_timeout.sh 60 $(BUILD_DIR)/test_filter_compliance --filter $(FILTER) --test lifecycle; \
	else \
		scripts/run_with_timeout.sh 60 $(BUILD_DIR)/test_filter_compliance --test lifecycle; \
	fi

compliance-dataflow: $(BUILD_DIR)/test_filter_compliance
	@echo "Running dataflow compliance tests..."
	@if [ -n "$(FILTER)" ]; then \
		scripts/run_with_timeout.sh 60 $(BUILD_DIR)/test_filter_compliance --filter $(FILTER) --test dataflow; \
	else \
		scripts/run_with_timeout.sh 60 $(BUILD_DIR)/test_filter_compliance --test dataflow; \
	fi

compliance-buffer: $(BUILD_DIR)/test_filter_compliance
	@echo "Running buffer configuration compliance tests..."
	@if [ -n "$(FILTER)" ]; then \
		scripts/run_with_timeout.sh 60 $(BUILD_DIR)/test_filter_compliance --filter $(FILTER) --test buffer; \
	else \
		scripts/run_with_timeout.sh 60 $(BUILD_DIR)/test_filter_compliance --test buffer; \
	fi

compliance-perf: $(BUILD_DIR)/test_filter_compliance
	@echo "Running performance compliance tests..."
	@if [ -n "$(FILTER)" ]; then \
		scripts/run_with_timeout.sh 60 $(BUILD_DIR)/test_filter_compliance --filter $(FILTER) --test perf; \
	else \
		scripts/run_with_timeout.sh 60 $(BUILD_DIR)/test_filter_compliance --test perf; \
	fi

# Linting targets
lint: lint-c #lint-py

lint-c: clang-format-check clang-tidy-check #cppcheck-check

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
	@cppcheck --enable=all --suppress=missingIncludeSystem --suppress=unusedFunction --suppress=truncLongCastReturn --error-exitcode=1 $(SRC_DIR)/

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

# Include dependency files
-include $(BUILD_DIR)/*.d
