# Error Handling Guide

This guide describes error handling patterns and best practices in the bpipe2 framework.

## Error Codes

All bpipe2 functions return `Bp_EC` (error code) enum values. The primary codes are:
- `Bp_EC_OK`: Success
- `Bp_EC_NULL_PTR`: Null pointer passed where not allowed
- `Bp_EC_ALLOC`: Memory allocation failure
- `Bp_EC_INVALID_ARG`: Invalid argument passed
- `Bp_EC_BUSY`: Resource is busy
- `Bp_EC_TIMEOUT`: Operation timed out
- `Bp_EC_OVERFLOW`: Buffer overflow
- `Bp_EC_UNDERFLOW`: Buffer underflow
- `Bp_EC_NOT_CONNECTED`: Filter not connected
- `Bp_EC_ALREADY_CONNECTED`: Filter already connected
- `Bp_EC_PTHREAD`: pthread operation failed
- `Bp_EC_EOF`: End of file/stream reached
- `Bp_EC_FILTER_STOPPING`: Filter is stopping (from bb_force_return)

## Error Handling Patterns

### 1. In Filter Implementation Code

Use direct error checking and propagation:

```c
Bp_EC signal_generator_init(SignalGenerator_t* sg, SignalGeneratorCfg_t cfg) {
    // Check arguments
    if (!sg) return Bp_EC_NULL_PTR;
    if (cfg.n_channels != 1) return Bp_EC_INVALID_ARG;
    
    // Propagate errors from called functions
    Bp_EC err = filter_init(&sg->base, "signal_generator", FILT_T_MAP, 0, 1);
    if (err != Bp_EC_OK) return err;
    
    // Initialize fields...
    return Bp_EC_OK;
}
```

### 2. In Worker Threads

Use `BP_WORKER_ASSERT` macro for critical errors that should terminate the worker:

```c
static void* signal_generator_worker(void* arg) {
    SignalGenerator_t* sg = (SignalGenerator_t*)arg;
    BP_WORKER_ASSERT(sg != NULL);
    
    while (atomic_load(&sg->base.running)) {
        Batch_t* output = bb_get_head(sg->base.sinks[0]);
        if (!output) {
            usleep(1000);
            continue;
        }
        
        // Generate samples...
        
        Bp_EC err = bb_submit(sg->base.sinks[0], 0);
        BP_WORKER_ASSERT(err == Bp_EC_OK);
    }
    
    return NULL;
}
```

### 3. In Test Code

Use `CHECK_ERR` macro from utils.h for clean test assertions:

```c
void test_signal_generator_basic(void) {
    SignalGenerator_t sg;
    SignalGeneratorCfg_t cfg = {
        .waveform = SAWTOOTH,
        .frequency_hz = 100.0,
        .amplitude = 1.0,
        .offset = 0.0,
        .phase_rad = 0.0,
        .period_ns = 1000000,
        .n_channels = 1,
        .max_samples = 256
    };
    
    // CHECK_ERR automatically asserts on non-OK error codes
    CHECK_ERR(signal_generator_init(&sg, cfg));
    
    TestSink_t sink;
    CHECK_ERR(test_sink_init(&sink, 1024));
    
    CHECK_ERR(bp_connect(&sg.base, 0, &sink.base, 0));
    CHECK_ERR(filter_start(&sg.base));
    CHECK_ERR(filter_start(&sink.base));
    
    // Wait for completion...
    
    // Check worker errors after pthread_join
    CHECK_ERR(sg.base.worker_err_info.ec);
    CHECK_ERR(sink.base.worker_err_info.ec);
}
```

## Error Context

### Worker Thread Errors

Worker threads store error information in `filter->worker_err_info`:
- `ec`: The error code that caused thread termination
- `line`: Line number where error occurred
- `file`: Source file where error occurred

Always check `worker_err_info.ec` after `pthread_join()` to detect worker thread failures.

### Error Message Lookup

Use the global `err_lut` array to get human-readable error messages:

```c
Bp_EC err = some_function();
if (err != Bp_EC_OK) {
    printf("Error: %s\n", err_lut[err]);
}
```

## Best Practices

1. **Check All Return Values**: Every bpipe2 function returns an error code - check it.

2. **Propagate Errors Up**: Don't silently ignore errors. Either handle them or propagate them.

3. **Use Appropriate Macros**:
   - `BP_WORKER_ASSERT`: In worker threads for unrecoverable errors
   - `CHECK_ERR`: In test code for clean assertions
   - Direct checks: In regular implementation code

4. **Initialize Error Codes**: When creating custom filters, initialize worker_err_info.ec to Bp_EC_OK.

5. **Fail Fast**: For configuration errors, fail during initialization rather than at runtime.

6. **Document Error Conditions**: In function documentation, list what error codes can be returned and when.

## Common Pitfalls

1. **Forgetting to Check Worker Errors**: Always check `filter->worker_err_info.ec` after joining worker threads.

2. **Using Wrong Assert Type**: Don't use regular `assert()` in worker threads - use `BP_WORKER_ASSERT`.

3. **Ignoring bb_submit Errors**: A failed `bb_submit()` often indicates a serious problem.

4. **Not Handling BUSY/TIMEOUT**: Some operations may temporarily fail - consider retry logic where appropriate.

## Example: Complete Error Handling

```c
Bp_EC process_batch(Filter_t* filter) {
    // Input validation
    if (!filter) return Bp_EC_NULL_PTR;
    if (!filter->sources[0]) return Bp_EC_NOT_CONNECTED;
    
    // Get input with timeout
    Batch_t* input = NULL;
    Bp_EC err = bb_get_tail_timeout(filter->sources[0], &input, 1000000000);
    if (err == Bp_EC_TIMEOUT) {
        // Timeout is not necessarily an error
        return Bp_EC_OK;
    }
    if (err != Bp_EC_OK) return err;
    
    // Process batch
    err = do_processing(input);
    if (err != Bp_EC_OK) {
        // Clean up before returning
        bb_return(filter->sources[0], 0);
        return err;
    }
    
    // Return batch
    err = bb_return(filter->sources[0], 0);
    return err;
}
```