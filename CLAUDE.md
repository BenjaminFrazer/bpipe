# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## MANDATORY: Before Starting ANY Work

**YOU MUST READ THESE DOCUMENTS FIRST:**

1. **Core Concepts (REQUIRED READING)**:
   - `docs/core_data_model.md` - MUST understand data structures, filter types, and architecture
   - `docs/filter_implementation_guide.md` - MUST learn filter patterns before implementing
   - `docs/public_api_reference.md` - MUST know API conventions
   - `docs/threading_model.md` - MUST understand threading requirements
   - `docs/error_handling_guide.md` - MUST follow error patterns

**IMPORTANT**: If you have not read the above documents, STOP and read them NOW. Do not proceed with any implementation work until you understand these core concepts. This is non-negotiable.

2. **For New Filter Development** (read AFTER core concepts):
   - Follow `docs/filter_development_checklist.md`
   - Reference `docs/zero_input_filter_patterns.md` for source filters
   - Use `docs/debugging_guide.md` when issues arise


3.  Once you've finished reading this file, Announce a list of the documents you have read. 

## Development Commands

### Building and Testing
- **Build all**: `make`
- **Run all C tests**: `make test-c`
- **Run specific test**: `make test-<name>` (e.g., `make test-signal-generator`)
- **Clean build artifacts**: `make clean`

### Test Timeout Utility
- **Purpose**: Prevent tests from hanging indefinitely
- **Usage**: `./scripts/run_with_timeout.sh <timeout_seconds> <executable> [args...]`
- **Example**: `./scripts/run_with_timeout.sh 30 ./build/test_core_filter`
- **Logging**: Timeout events are logged to `timeout.log`
- **Exit codes**: 0=success, 1=test failure, 124=timeout, 2=usage error

### Linting and Code Quality
- **Run all linting**: `make lint` (runs both C and Python checks)
- **C code linting**: `make lint-c` (format check, static analysis, cppcheck)
- **Python code linting**: `make lint-py` (ruff linting and format check)
- **Auto-fix issues**: `make lint-fix` (fixes formatting and auto-fixable issues)
- **Individual tools**: `make clang-format-check`, `make clang-tidy-check`, `make cppcheck-check`, `make ruff-check`
- **IMPORTANT**: Always run `make lint` before committing changes

### Linter Version Consistency
To ensure your local linters match CI versions:
1. **Run setup script**: `./scripts/setup-linters.sh` (installs clang-format-14 and clang-tidy-14)
2. **CI uses**: Ubuntu latest with clang-format-14 and clang-tidy-14
3. **Config file**: `.clang-format` ensures consistent formatting rules across environments

## Project Architecture

This is a **modular real-time telemetry data processing framework** built in C with Python bindings. The system implements a push-based data processing pipeline using directed acyclic graphs (DAGs).

### Additional Documentation
- **Specialized Topics**:
  - `docs/multi-input_syncronisation.md` - Multi-input synchronization
  - `docs/testing_guidelines.md` - Testing best practices
  - `docs/performance_considerations.md` - Performance optimization
  - `docs/build_system_guide.md` - Build system details

### Build System
- **C compilation**: Makefile with gcc, Unity testing framework
- **Python extension**: setuptools with C extension module
- **Test framework**: Unity (lib/Unity/) for C unit tests
- **Compiler flags**: `-std=c99 -Wall -Werror -pthread`

### Key Files
- `bpipe/core.h`: Core data structures and inline functions
- `bpipe/core.c`: Main filter initialization and worker thread logic
- `bpipe/batch_buffer.c`: Data buffer implementation
- `bpipe/utils.h`: Common utilities and CHECK_ERR macro
- `tests/`: Unity-based test suite
- `specs/`: Design specifications for filters

### Common Pitfalls
1. **Missing bb_submit()**: Always submit output batches after filling
2. **Worker thread not exiting**: Check atomic_load(&running) in loops
3. **Wrong filter type**: Use FILT_T_MAP (FILT_T_SOURCE doesn't exist)
4. **Not checking worker errors**: Check filter->worker_err_info.ec after pthread_join
5. **Forgetting batch metadata**: Set t_ns, period_ns, head, tail, ec

## Code Philosophy
- **No legacy API support**: When APIs change, migrate everything
- **No try-catch**: Use error codes and BP_WORKER_ASSERT
- **No unnecessary abstraction**: Keep it simple and direct
- **No redundant state**: Single source of truth
- **Fail fast**: Validate configuration during init, not runtime
- **Document intent**: Comments should explain why, not what

## Testing Philosophy
- **Use CHECK_ERR macro**: Consistent error checking in tests
- **Test edge cases**: Empty batches, max sizes, boundary conditions
- **Use timeout wrapper**: Prevent hanging tests
- **Check worker errors**: Always verify worker_err_info.ec
- **Document test intent**: Each test should have a description comment

## Important Reminders
- **Keep documentation updated**: Update docs when changing functionality
- **Don't be a sycophant**: Correct errors when you see them
- **Run linting**: Always `make lint` before committing
- **Test thoroughly**: Run tests with timeout wrapper
- **Check error codes**: Every function returns Bp_EC - check it

