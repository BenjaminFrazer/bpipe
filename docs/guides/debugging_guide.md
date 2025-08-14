# Debugging Guide

This guide provides strategies and tools for debugging common issues in bpipe2 filters and pipelines.

## Debugging Hanging Filters

### Symptoms
- Test executable hangs indefinitely
- `make test` never completes
- Process must be killed with Ctrl+C

### Initial Diagnosis

1. **Use the timeout script**:
```bash
./scripts/run_with_timeout.sh 30 ./build/test_my_filter
# Check timeout.log for details
```

2. **Run with strace**:
```bash
strace -f ./build/test_my_filter 2>&1 | tail -20
# Look for:
# - futex(...) - waiting on mutex/condition
# - read(...) - blocked on I/O
# - nanosleep(...) - sleeping
```

3. **Check thread states**:
```bash
# While test is running
ps -eLf | grep test_my_filter
# Look at thread states (S column)
# R = running, S = sleeping, D = disk wait
```

### Common Causes

#### 1. Missing bb_submit()
```c
// BAD - buffer never submitted
Batch_t* output = bb_get_head(sink);
generate_data(output);
// Missing: bb_submit(sink, 0);

// GOOD
Batch_t* output = bb_get_head(sink);
generate_data(output);
bb_submit(sink, 0);  // Don't forget!
```

#### 2. Worker Thread Not Exiting
```c
// BAD - doesn't check running flag
void* worker(void* arg) {
    while (1) {  // Infinite loop!
        process_data();
    }
}

// GOOD - checks atomic flag
void* worker(void* arg) {
    Filter_t* f = (Filter_t*)arg;
    while (atomic_load(&f->running)) {
        process_data();
    }
}
```

#### 3. Deadlock in Buffer Access
```c
// BAD - holding buffer while waiting for another
Batch_t* input = bb_get_tail(source);
Batch_t* output = bb_get_head(sink);  // May deadlock!

// GOOD - get output first or handle failure
Batch_t* output = bb_get_head(sink);
if (!output) {
    bb_return(source, 0);  // Release input
    continue;
}
```

#### 4. Circular Wait
```c
// Pipeline: A -> B -> C -> A (circular!)
// Each filter waiting for next in circle = deadlock

// Solution: Break circular dependency
```

### Debugging Techniques

#### Add Debug Prints
```c
#define DEBUG_PRINT(fmt, ...) \
    fprintf(stderr, "[%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

void* worker(void* arg) {
    DEBUG_PRINT("Worker started");
    
    while (atomic_load(&running)) {
        DEBUG_PRINT("Getting buffer...");
        Batch_t* b = bb_get_head(sink);
        DEBUG_PRINT("Got buffer: %p", b);
        // ...
    }
    
    DEBUG_PRINT("Worker exiting");
}
```

#### Use Debug Output Filter

For debugging data flow through pipelines, insert the Debug Output filter at strategic points:

```c
#include "bpipe/debug_output_filter.h"

// Insert debug filter to trace data between processing stages
void debug_pipeline(void) {
    // Original pipeline: Source -> Process1 -> Process2 -> Sink
    // Debug pipeline:   Source -> Debug1 -> Process1 -> Debug2 -> Process2 -> Sink
    
    DebugOutputFilter_t debug1, debug2;
    
    // Debug after source to verify input data
    DebugOutputConfig_t debug1_config = {
        .prefix = "[SOURCE] ",
        .show_metadata = true,
        .show_samples = true,
        .max_samples_per_batch = 5,  // Limit output
        .format = DEBUG_FMT_DECIMAL,
        .flush_after_print = true,
        .filename = NULL  // stdout
    };
    debug_output_filter_init(&debug1, &debug1_config);
    
    // Debug after first processor
    DebugOutputConfig_t debug2_config = {
        .prefix = "[PROCESS1] ",
        .show_metadata = true,
        .show_samples = false,  // Only show metadata
        .format = DEBUG_FMT_DECIMAL,
        .flush_after_print = true,
        .filename = "pipeline_trace.log"
    };
    debug_output_filter_init(&debug2, &debug2_config);
    
    // Connect pipeline with debug points
    bp_connect(&source, 0, &debug1.base, 0);
    bp_connect(&debug1.base, 0, &process1, 0);
    bp_connect(&process1, 0, &debug2.base, 0);
    bp_connect(&debug2.base, 0, &process2, 0);
    bp_connect(&process2, 0, &sink, 0);
}
```

**Debug Output Filter Use Cases:**

1. **Verify Source Data**:
```c
// Check what data is coming from source
DebugOutputConfig_t config = {
    .prefix = "[RAW_INPUT] ",
    .show_samples = true,
    .max_samples_per_batch = -1,  // Show all
    .format = DEBUG_FMT_HEX,      // Useful for binary protocols
    .filename = "raw_input.log"
};
```

2. **Track Timing Issues**:
```c
// Focus on batch timing metadata
DebugOutputConfig_t config = {
    .prefix = "[TIMING] ",
    .show_metadata = true,
    .show_samples = false,  // Only metadata
    .flush_after_print = true
};
```

3. **Compare Before/After Processing**:
```c
// Place debug filters before and after a suspicious filter
DebugOutputFilter_t before, after;

// Same format for easy comparison
DebugOutputConfig_t config = {
    .format = DEBUG_FMT_SCIENTIFIC,
    .max_samples_per_batch = 10
};

config.prefix = "[BEFORE] ";
debug_output_filter_init(&before, &config);

config.prefix = "[AFTER] ";
debug_output_filter_init(&after, &config);
```

4. **Monitor Long-Running Pipelines**:
```c
// Log to file with timestamps
DebugOutputConfig_t config = {
    .prefix = "[MONITOR] ",
    .show_metadata = true,
    .show_samples = false,
    .filename = "/tmp/pipeline_monitor.log",
    .append_mode = true,  // Don't overwrite
    .flush_after_print = true  // Ensure writes
};
```

**Tips for Using Debug Output Filter:**
- Start with `max_samples_per_batch` limited to avoid flooding output
- Use different prefixes to distinguish multiple debug points
- Log to files for long runs to avoid terminal overflow
- Disable sample printing (`show_samples = false`) for high-frequency data
- Use hex or binary format for debugging binary protocols
- Remember to remove debug filters from production code

For more examples and use cases, see [Debug Output Filter Examples](debug_output_filter_examples.md).

#### Use GDB
```bash
# Compile with debug symbols
make CFLAGS="-g -O0"

# Run in GDB
gdb ./build/test_my_filter
(gdb) run
# When it hangs, press Ctrl+C
(gdb) info threads
(gdb) thread apply all bt  # Show all backtraces
```

#### Check Library Loading
```bash
# Library issues can cause hangs during startup
ldd ./build/test_my_filter
# Check for "not found" libraries

# Trace library calls
ltrace ./build/test_my_filter
```

## Debugging Crashes

### Segmentation Faults

#### Use Address Sanitizer
```bash
# Compile with ASan
make CFLAGS="-fsanitize=address -g"
./build/test_my_filter
# ASan will report memory errors with stack traces
```

#### Common Causes
```c
// 1. Null pointer dereference
Filter_t* f = NULL;
f->name = "test";  // Crash!

// 2. Buffer overflow
float buffer[100];
for (int i = 0; i <= 100; i++) {  // Off by one!
    buffer[i] = 0;
}

// 3. Use after free
free(data);
data[0] = 1.0;  // Crash!

// 4. Uninitialized pointer
float* data;  // Uninitialized!
data[0] = 1.0;  // Crash!
```

### Stack Traces

#### Enable Core Dumps
```bash
ulimit -c unlimited
./build/test_my_filter
# If it crashes, creates core file

# Analyze core
gdb ./build/test_my_filter core
(gdb) bt  # Backtrace
(gdb) info locals  # Local variables
```

#### Built-in Backtrace
```c
#include <execinfo.h>
#include <signal.h>

void signal_handler(int sig) {
    void* array[20];
    size_t size = backtrace(array, 20);
    
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    exit(1);
}

// In main()
signal(SIGSEGV, signal_handler);
signal(SIGABRT, signal_handler);
```

## Debugging Data Corruption

### Symptoms
- Wrong output values
- Intermittent test failures
- Values change unexpectedly

### Memory Sanitizers

```bash
# Thread Sanitizer - for race conditions
make CFLAGS="-fsanitize=thread -g"

# Memory Sanitizer - for uninitialized memory
make CFLAGS="-fsanitize=memory -g"

# Undefined Behavior Sanitizer
make CFLAGS="-fsanitize=undefined -g"
```

### Valgrind

```bash
# Memory errors
valgrind --leak-check=full --track-origins=yes \
         ./build/test_my_filter

# Race conditions
valgrind --tool=helgrind ./build/test_my_filter

# Cache profiling
valgrind --tool=cachegrind ./build/test_my_filter
```

### Common Issues

#### Race Conditions
```c
// BAD - unsynchronized access
int counter = 0;
void* thread1(void* arg) { counter++; }
void* thread2(void* arg) { counter++; }

// GOOD - use atomics
atomic_int counter = 0;
void* thread1(void* arg) { atomic_fetch_add(&counter, 1); }
```

#### Buffer Overruns
```c
// Add bounds checking
void process(float* data, size_t n) {
    assert(n <= MAX_SIZE);
    for (size_t i = 0; i < n; i++) {
        assert(i < n);  // Paranoid check
        data[i] = compute(i);
    }
}
```

## Debugging Performance Issues

### Profiling Tools

#### perf
```bash
# Record profile
perf record -g ./build/test_my_filter

# Analyze
perf report

# Real-time top
perf top -p $(pidof test_my_filter)
```

#### gprof
```bash
# Compile with profiling
make CFLAGS="-pg"
./build/test_my_filter

# Analyze
gprof ./build/test_my_filter gmon.out
```

### Timing Analysis

```c
// Add timing to critical sections
typedef struct {
    uint64_t start_ns;
    uint64_t total_ns;
    uint64_t count;
} TimingStats_t;

void measure_section(TimingStats_t* stats) {
    uint64_t start = get_time_ns();
    
    // Critical section
    do_work();
    
    uint64_t elapsed = get_time_ns() - start;
    stats->total_ns += elapsed;
    stats->count++;
    
    if (stats->count % 1000 == 0) {
        printf("Average time: %.2f us\n", 
               (stats->total_ns / stats->count) / 1000.0);
    }
}
```

## Debugging Test Failures

### Unity Test Framework

#### Verbose Output
```c
// Add messages to assertions
TEST_ASSERT_EQUAL_MESSAGE(expected, actual, 
    "Failed at sample 42");

// Print values on failure
TEST_ASSERT_EQUAL_FLOAT_MESSAGE(1.0f, value,
    "Expected 1.0 but got " + value);
```

#### Test Isolation
```c
void setUp(void) {
    // Reset global state before each test
    reset_error_counters();
    clear_buffers();
}

void tearDown(void) {
    // Clean up after each test
    free_resources();
}
```

### Debugging Intermittent Failures

```c
// Run test multiple times
void test_concurrent_access(void) {
    // Run same test 100 times to catch race conditions
    for (int i = 0; i < 100; i++) {
        printf("Iteration %d\n", i);
        
        // Test setup
        Filter_t f;
        filter_init(&f);
        
        // Run test
        run_concurrent_test(&f);
        
        // Cleanup
        filter_deinit(&f);
    }
}
```

## Common Error Patterns

### 1. Startup Hangs

```c
// Check static initialization
__attribute__((constructor))
void init(void) {
    printf("Init called\n");  // Does this print?
    // Hanging here?
}
```

### 2. Shutdown Hangs

```c
// Ensure all threads can exit
void stop_filter(Filter_t* f) {
    atomic_store(&f->running, false);
    
    // Wake up any blocked threads
    pthread_cond_broadcast(&f->cond);
    
    // Interrupt blocking I/O
    if (f->fd >= 0) {
        shutdown(f->fd, SHUT_RDWR);
    }
}
```

### 3. Memory Leaks

```c
// Track allocations
typedef struct {
    void* ptr;
    size_t size;
    const char* file;
    int line;
} Allocation_t;

#define MALLOC(size) debug_malloc(size, __FILE__, __LINE__)
#define FREE(ptr) debug_free(ptr)

void* debug_malloc(size_t size, const char* file, int line) {
    void* ptr = malloc(size);
    record_allocation(ptr, size, file, line);
    return ptr;
}
```

## Debug Build Configuration

### Makefile Debug Target

```makefile
debug: CFLAGS += -g -O0 -DDEBUG
debug: CFLAGS += -fsanitize=address
debug: CFLAGS += -fno-omit-frame-pointer
debug: all

# Usage: make debug
```

### Debug Macros

```c
#ifdef DEBUG
#define DBG_PRINT(...) fprintf(stderr, __VA_ARGS__)
#define DBG_ASSERT(x) assert(x)
#else
#define DBG_PRINT(...)
#define DBG_ASSERT(x)
#endif
```

## Debugging Checklist

- [ ] **For hangs**:
  - [ ] Check all worker threads exit properly
  - [ ] Verify bb_submit() called for all buffers
  - [ ] Look for circular dependencies
  - [ ] Check atomic running flags
  - [ ] Use timeout script

- [ ] **For crashes**:
  - [ ] Enable core dumps
  - [ ] Use address sanitizer
  - [ ] Check for null pointers
  - [ ] Verify array bounds
  - [ ] Look for use-after-free

- [ ] **For wrong output**:
  - [ ] Add debug prints at key points
  - [ ] Verify input data is correct
  - [ ] Check for race conditions
  - [ ] Test with single thread
  - [ ] Use memory sanitizer

- [ ] **For performance**:
  - [ ] Profile with perf
  - [ ] Check for busy-waiting
  - [ ] Measure critical sections
  - [ ] Verify batch sizes
  - [ ] Look for lock contention

## Quick Reference

```bash
# Debug hanging test
./scripts/run_with_timeout.sh 30 ./build/test_filter
strace -f ./build/test_filter 2>&1 | tail -20

# Debug crash
make CFLAGS="-fsanitize=address -g"
gdb ./build/test_filter
(gdb) run
(gdb) bt

# Debug memory issues
valgrind --leak-check=full ./build/test_filter

# Debug performance
perf record -g ./build/test_filter
perf report
```