# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Development Commands

### Building and Testing
- **Build all tests**: `make all`
- **Run all tests**: `make run`
- **Clean build artifacts**: `make clean`
- **Build specific tests**: `make test_core_filter`, `make test_signal_gen`, `make test_sentinel`
- **Build Python extension**: `python setup.py build_ext --inplace`

### Task Management
- **Get task information**: `./get_task.sh <TASK_ID> tasks.md` (e.g., `./get_task.sh OBJ_INIT tasks.md`)
- **Current branch**: OBJ_INIT (working on object initialization tasks)

## Project Architecture

This is a **modular real-time telemetry data processing framework** built in C with Python bindings. The system implements a push-based data processing pipeline using directed acyclic graphs (DAGs).

### Core Components

#### Filter Architecture (`Bp_Filter_t`)
- **Base component**: All processing elements inherit from `Bp_Filter_t` (bpipe/core.h:85-99)
- **Transform functions**: Components process data via `TransformFcn_t` callbacks
- **Threading**: Each filter runs in its own pthread worker thread
- **Buffering**: Ring buffer system with configurable capacity and overflow behavior
- **Error handling**: Comprehensive error codes via `Bp_EC` enum

#### Batch Processing (`Bp_Batch_t`)
- **Data structure**: Batches contain metadata (timestamps, sequence numbers) and payload data
- **Types**: Support for float, int, unsigned data types via `SampleDtype_t`
- **Ring buffers**: Circular buffer implementation with exponential capacity sizing

#### Specialized Filters
- **Signal Generator** (`Bp_SignalGen_t`): Generates waveforms (square, sine, triangle, sawtooth)
- **Python Filters** (`BpFilterPy_t`): Allows custom Python transform functions via CPython API

### Data Flow
1. Components connect via `.set_sink()` method
2. Data flows through ring buffers between components
3. Backpressure handling: configurable to block or drop samples on overflow
4. Thread synchronization via pthread mutexes and condition variables

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

### Task Workflow
- Branch naming: Create branches named after TASK_ID from `tasks.md`
- Work logging: Record changes in `<TASK_ID>_log.md` files
- Task lookup: Use `get_task.sh` script to extract task-specific information
- Current focus: Object initialization (OBJ_INIT task)