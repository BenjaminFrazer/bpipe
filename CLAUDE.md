# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Development Commands

### Building and Testing
- **Build all tests**: `make all`
- **Run all tests**: `make run`
- **Clean build artifacts**: `make clean`
- **Build specific tests**: `make test_core_filter`, `make test_signal_gen`, `make test_sentinel`
- **Build Python extension**: `python setup.py build_ext --inplace`

### Test Timeout Utility
- **Purpose**: Prevent tests from hanging indefinitely (e.g., test_core_filter timing issue)
- **Usage**: `./run_with_timeout.sh <timeout_seconds> <executable> [args...]`
- **Example**: `./run_with_timeout.sh 30 ./build/test_core_filter`
- **Make target**: `make run-safe` runs all tests with 30s timeout
- **Logging**: Timeout events are logged to `timeout.log`
- **Exit codes**: 0=success, 1=test failure, 124=timeout, 2=usage error

### Linting and Code Quality
- **Run all linting**: `make lint` (runs both C and Python checks)
- **C code linting**: `make lint-c` (format check, static analysis, cppcheck)
- **Python code linting**: `make lint-py` (ruff linting and format check)
- **Auto-fix issues**: `make lint-fix` (fixes formatting and auto-fixable issues)
- **Individual tools**: `make clang-format-check`, `make clang-tidy-check`, `make cppcheck-check`, `make ruff-check`

### Task Management
- **Get task information**: `./get_task.sh <TASK_ID> tasks.md` (e.g., `./get_task.sh OBJ_INIT tasks.md`)
- **Current branch**: OBJ_INIT (working on object initialization tasks)

## Project Architecture

This is a **modular real-time telemetry data processing framework** built in C with Python bindings. The system implements a push-based data processing pipeline using directed acyclic graphs (DAGs).

### documentation
- key documentation to read is:
    - core data model : `docs/core_datamodel.md`
    - multi-input data sync: `docs/multi_input_syncronisation.md`
- keep documentation updated.

### Build System
- **C compilation**: Makefile with gcc, Unity testing framework
- **Python extension**: setuptools with C extension module
- **Test framework**: Unity (lib/Unity/) for C unit tests
- **Compiler flags**: `-std=c99 -Wall -Werror -pthread -save-temps`

### Key Files
- `bpipe/core.h`: Core data structures and inline functions
- `bpipe/core.c`: Main filter initialization and worker thread logic
- `bpipe/signal_gen.h/.c`: Signal generation components
- `bpipe/core_python.h/.c`: Python C API bindings
- `tests/`: Unity-based test suite
- `requirements.adoc`: Detailed system requirements and architecture specs

### MISC
- dont't be a sycophant, correct me if I'm wrong

### Planning
- on exiting plan mode for a new feature claude MUST writing the plan to a markdown file in the specs/ dir. 
- the plan should have a todo list, check off items as you progress 

### Issues
- Issues are tracked in the `issues/` folder as markdown files, one per issue.
- open issues are pre-fixed with "OPEN" 
- closed issues are pre-fixed with "CLOSED"
- an issue should identify which tests are failing and the failure signature.

### Task Workflow
- Task lookup: Use `get_task.sh` script to extract task info `tasks.md`
- create a plan for a new feature in in plan mode and commit to main
- Branch naming: Create branches named after TASK_ID from `tasks.md`
- all work on source-code MUST be conducted in git worktrees to isolate agents from one-another.
- Work logging: Record changes in `<TASK_ID>_log.md` files
- git worktree are created under the /trees directory.
- many agents may be working in paralel rebase from main regularly

### Task completion
- commit code under feature branch before asking for review.
- create a summary of task status, out-standing issues and judgement of whether code is production ready.
- once code is pushed to main remove task worktree.

### Code Philosophy
- do not support legacy api's if the api changes migrate everything.
- avoid try catches
- no uneccessary layers of abstraction
- Avoid redundant state.
## Development Notes

- We are actively developing the API DO NOT maintain backwards compatibility.
