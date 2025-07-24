# Build System Guide

This guide explains the bpipe2 build system, how to add new components, and how to resolve common build issues.

## Overview

Bpipe2 uses a Makefile-based build system for C code and setuptools for Python bindings.

### Directory Structure

```
bpipe2/
├── Makefile              # Main build file
├── bpipe/               # C source files
│   ├── *.h             # Header files
│   └── *.c             # Implementation files
├── tests/              # Test files
│   └── test_*.c        # Unity test files
├── build/              # Build artifacts (generated)
│   ├── *.o            # Object files
│   └── test_*         # Test executables
├── lib/               # External libraries
│   └── Unity/         # Unity test framework
├── python/            # Python bindings
│   ├── setup.py      # Python build configuration
│   └── src/          # Python C extension source
└── scripts/          # Utility scripts
    └── run_with_timeout.sh
```

## Make Targets

### Primary Targets

```bash
make              # Build all (library + tests)
make clean        # Remove build artifacts
make test         # Run all tests
make test-c       # Run C tests only
make lint         # Run all linting checks
make lint-fix     # Auto-fix linting issues
```

### Linting Targets

```bash
make lint-c              # All C linting checks
make clang-format-check  # Check C formatting
make clang-tidy-check    # Static analysis
make cppcheck-check      # Additional static analysis
make lint-py             # Python linting
make ruff-check          # Python style check
```

### Test Targets

```bash
make test-<name>  # Run specific test
# Example: make test-signal-generator
```

## Adding a New Filter

### 1. Create Source Files

```bash
# Create header
touch bpipe/my_filter.h

# Create implementation
touch bpipe/my_filter.c

# Create test
touch tests/test_my_filter.c
```

### 2. Update Makefile

Add source file to SRC_FILES:

```makefile
SRC_FILES = \
    bpipe/core.c \
    bpipe/batch_buffer.c \
    bpipe/my_filter.c \    # Add your filter here
    # ... other files
```

Add test target:

```makefile
TEST_TARGETS = \
    build/test_core \
    build/test_my_filter \  # Add your test here
    # ... other tests
```

Add test rule:

```makefile
test-my-filter: build/test_my_filter
	./scripts/run_with_timeout.sh 30 ./build/test_my_filter
```

### 3. Build and Test

```bash
make clean          # Clean previous build
make               # Build everything
make test-my-filter # Run your test
```

## Compiler Flags

### Standard Flags

```makefile
CFLAGS = -g -std=c99 -D_DEFAULT_SOURCE -Wall -Werror -pthread -rdynamic
```

- `-g`: Debug symbols
- `-std=c99`: C99 standard
- `-D_DEFAULT_SOURCE`: Enable POSIX functions
- `-Wall -Werror`: All warnings as errors
- `-pthread`: Thread support
- `-rdynamic`: Export symbols for backtrace

### Debug Build

```bash
make CFLAGS="-g -O0 -DDEBUG"
```

### Release Build

```bash
make CFLAGS="-O3 -DNDEBUG"
```

## Dependencies

### System Dependencies

Required packages (Ubuntu/Debian):

```bash
sudo apt-get install \
    build-essential \
    clang \
    clang-format \
    clang-tidy \
    cppcheck \
    python3-dev
```

### Unity Test Framework

Unity is included in `lib/Unity/`. No installation needed.

## Common Build Issues

### 1. Missing Header

**Error**: `fatal error: bpipe/my_filter.h: No such file or directory`

**Solution**: Ensure header exists and include path is correct:
```c
#include "bpipe/my_filter.h"  // Correct
#include "my_filter.h"        // Wrong - missing bpipe/
```

### 2. Undefined Reference

**Error**: `undefined reference to 'my_filter_init'`

**Solution**: Add source file to SRC_FILES in Makefile:
```makefile
SRC_FILES = \
    # ... other files
    bpipe/my_filter.c
```

### 3. Multiple Definition

**Error**: `multiple definition of 'my_function'`

**Solution**: 
- Don't define functions in headers (only declare)
- Use `static` for internal functions
- Use include guards in headers

### 4. Incompatible Function Signature

**Error**: `incompatible function pointer types`

**Solution**: Ensure function signatures match exactly:
```c
// Worker function must have this exact signature
static void* my_filter_worker(void* arg)
```

### 5. Missing pthread Functions

**Error**: `undefined reference to 'pthread_create'`

**Solution**: Already handled by `-pthread` flag. If persists, check:
```bash
gcc --version  # Ensure gcc is installed
```

## Python Extension Build

### Building Python Module

```bash
cd python
pip install -e .  # Development install
# or
python setup.py build_ext --inplace
```

### Adding Filter to Python

1. Add to `python/src/bpipe_module.c`
2. Create wrapper in `python/bpipe/filters/`
3. Update `python/bpipe/__init__.py`

## Linting and Formatting

### Auto-format C Code

```bash
make clang-format  # Format all C files
```

### Format Configuration

`.clang-format` file defines style:
- Line width: 100
- Indent: 4 spaces
- Brace style: Attach

### Static Analysis

```bash
make clang-tidy-check  # Run clang-tidy
make cppcheck-check    # Run cppcheck
```

Fix common issues:
- Unused variables
- Potential null dereferences
- Resource leaks

## Debugging Build Issues

### Verbose Build

```bash
make V=1  # Show all commands
```

### Check Dependencies

```bash
make -p | grep "^build/my_filter.o"  # Show dependencies
```

### Clean Build

```bash
make clean
make -j1  # Single-threaded build for clearer errors
```

### Preprocessor Output

```bash
gcc -E bpipe/my_filter.c > preprocessed.c  # Expand macros
```

## Performance Optimization

### Compiler Optimizations

```makefile
# Development
CFLAGS += -O0 -g

# Performance testing
CFLAGS += -O2 -march=native

# Maximum optimization
CFLAGS += -O3 -march=native -flto
```

### Profile-Guided Optimization

```bash
# Build with profiling
make CFLAGS="-fprofile-generate"

# Run representative workload
./build/test_performance

# Rebuild with profile
make clean
make CFLAGS="-fprofile-use"
```

## Continuous Integration

### GitHub Actions

The project uses GitHub Actions for CI. See `.github/workflows/`

### Local CI Simulation

```bash
# Run all checks locally
make clean
make
make test
make lint
```

## Troubleshooting

### Test Timeouts

If tests hang, use timeout wrapper:

```bash
./scripts/run_with_timeout.sh 30 ./build/test_my_filter
```

Check `timeout.log` for timeout events.

### Memory Issues

Use valgrind for memory debugging:

```bash
valgrind --leak-check=full ./build/test_my_filter
```

### Thread Issues

Use thread sanitizer:

```bash
make CFLAGS="-fsanitize=thread -g"
./build/test_my_filter
```

## Best Practices

1. **Always run `make clean` after Makefile changes**
2. **Use `make -j$(nproc)` for parallel builds**
3. **Run `make lint` before committing**
4. **Add new files to both SRC_FILES and git**
5. **Keep build times fast - minimize dependencies**
6. **Use timeout wrapper for all tests**
7. **Document build requirements in README**