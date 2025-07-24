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