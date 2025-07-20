# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Development Commands

### Building and Testing
- **Run all c tests**: `make test-c`
- **Clean build artifacts**: `make clean`

### Test Timeout Utility
- **Purpose**: Prevent tests from hanging indefinitely (e.g., test_core_filter timing issue)
- **Usage**: `./run_with_timeout.sh <timeout_seconds> <executable> [args...]`
- **Example**: `./run_with_timeout.sh 30 ./build/test_core_filter`
- **Logging**: Timeout events are logged to `timeout.log`
- **Exit codes**: 0=success, 1=test failure, 124=timeout, 2=usage error

### Linting and Code Quality
- **Run all linting**: `make lint` (runs both C and Python checks)
- **C code linting**: `make lint-c` (format check, static analysis, cppcheck)
- **Python code linting**: `make lint-py` (ruff linting and format check)
- **Auto-fix issues**: `make lint-fix` (fixes formatting and auto-fixable issues)
- **Individual tools**: `make clang-format-check`, `make clang-tidy-check`, `make cppcheck-check`, `make ruff-check`

## Project Architecture

This is a **modular real-time telemetry data processing framework** built in C with Python bindings. The system implements a push-based data processing pipeline using directed acyclic graphs (DAGs).

### documentation
- key documentation to read is:
    - core data model : `docs/core_data_model.md`
    - multi-input data sync: `docs/multi-input_syncronisation.md`
    - filter implementation guide: `docs/filter_implementation_guide.md`
- keep documentation updated.

### Build System
- **C compilation**: Makefile with gcc, Unity testing framework
- **Python extension**: setuptools with C extension module
- **Test framework**: Unity (lib/Unity/) for C unit tests
- **Compiler flags**: `-std=c99 -Wall -Werror -pthread -save-temps`

### Key Files
- `bpipe/core.h`: Core data structures and inline functions
- `bpipe/core.c`: Main filter initialization and worker thread logic
- `bpipe/batch_buffer.c`: Data buffer implementation.
- `tests/`: Unity-based test suite

### MISC
- dont't be a sycophant, correct me if I'm wrong

### Code Philosophy
- do not support legacy api's if the api changes migrate everything.
- avoid try catches
- no uneccessary layers of abstraction
- Avoid redundant state.
