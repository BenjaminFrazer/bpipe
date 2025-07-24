# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Quick Start for New Agents

1. **Read Core Documentation First**:
   - `docs/core_data_model.md` - Understand data structures
   - `docs/filter_implementation_guide.md` - Learn filter patterns
   - `docs/public_api_reference.md` - API conventions
   - `docs/threading_model.md` - Threading requirements
   - `docs/error_handling_guide.md` - Error patterns

2. **For New Filter Development**:
   - Follow `docs/filter_development_checklist.md`
   - Reference `docs/zero_input_filter_patterns.md` for source filters
   - Use `docs/debugging_guide.md` when issues arise

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

## Project Architecture

This is a **modular real-time telemetry data processing framework** built in C with Python bindings. The system implements a push-based data processing pipeline using directed acyclic graphs (DAGs).

### Key Documentation
- **Core Concepts**:
  - `docs/core_data_model.md` - Data structures and core concepts
  - `docs/multi-input_syncronisation.md` - Multi-input synchronization
  - `docs/filter_implementation_guide.md` - How to implement filters
  - `docs/public_api_reference.md` - Public API reference
  
- **Development Guides**:
  - `docs/filter_development_checklist.md` - Step-by-step filter creation
  - `docs/error_handling_guide.md` - Error handling patterns
  - `docs/threading_model.md` - Threading and synchronization
  - `docs/testing_guidelines.md` - Testing best practices
  
- **Specialized Topics**:
  - `docs/zero_input_filter_patterns.md` - Source filter patterns
  - `docs/performance_considerations.md` - Performance optimization
  - `docs/build_system_guide.md` - Build system details
  - `docs/debugging_guide.md` - Debugging strategies

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