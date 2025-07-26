# Debug Output Filter Examples

This document provides practical examples of using the Debug Output Filter for various debugging scenarios in bpipe2 pipelines.

## Quick Reference

```c
#include "bpipe/debug_output_filter.h"

// Basic usage
DebugOutputFilter_t debug;
DebugOutputConfig_t config = {
    .prefix = "[DEBUG] ",
    .show_metadata = true,
    .show_samples = true,
    .max_samples_per_batch = 10,
    .format = DEBUG_FMT_DECIMAL,
    .flush_after_print = true,
    .filename = NULL,  // stdout
    .append_mode = false
};
debug_output_filter_init(&debug, &config);
```

## Common Debugging Scenarios

### 1. Debugging Empty Output

**Problem**: Your sink isn't receiving any data.

**Solution**: Insert debug filters to trace where data stops flowing.

```c
// Insert debug filters at each stage
DebugOutputFilter_t debug_source, debug_process, debug_sink;

// Configure to show batch count only
DebugOutputConfig_t trace_config = {
    .prefix = "",  // Will add custom prefix per stage
    .show_metadata = true,
    .show_samples = false,
    .flush_after_print = true
};

// Track source output
trace_config.prefix = "[SOURCE->] ";
debug_output_filter_init(&debug_source, &trace_config);

// Track after processing
trace_config.prefix = "[PROCESS->] ";
debug_output_filter_init(&debug_process, &trace_config);

// Track before sink
trace_config.prefix = "[->SINK] ";
debug_output_filter_init(&debug_sink, &trace_config);

// Wire up pipeline with debug points
bp_connect(&source, 0, &debug_source.base, 0);
bp_connect(&debug_source.base, 0, &processor, 0);
bp_connect(&processor, 0, &debug_process.base, 0);
bp_connect(&debug_process.base, 0, &sink, 0);
```

### 2. Verifying Data Transformations

**Problem**: Output data doesn't match expected values.

**Solution**: Compare data before and after transformation.

```c
// Debug configuration for data comparison
DebugOutputConfig_t compare_config = {
    .show_metadata = false,  // Focus on data
    .show_samples = true,
    .max_samples_per_batch = 20,
    .format = DEBUG_FMT_SCIENTIFIC,  // Good for floating point
    .flush_after_print = true,
    .filename = "transform_comparison.log"
};

DebugOutputFilter_t before, after;

// Before transformation
compare_config.prefix = "BEFORE: ";
debug_output_filter_init(&before, &compare_config);

// After transformation
compare_config.prefix = "AFTER:  ";  // Same width for alignment
compare_config.append_mode = true;    // Append to same file
debug_output_filter_init(&after, &compare_config);

// Insert around suspicious filter
bp_connect(&source, 0, &before.base, 0);
bp_connect(&before.base, 0, &transform_filter, 0);
bp_connect(&transform_filter, 0, &after.base, 0);
bp_connect(&after.base, 0, &sink, 0);
```

### 3. Debugging Timing Issues

**Problem**: Batches have incorrect timestamps or periods.

**Solution**: Focus on metadata only.

```c
DebugOutputConfig_t timing_config = {
    .prefix = "[TIMING] ",
    .show_metadata = true,
    .show_samples = false,  // Don't care about data
    .flush_after_print = true,
    .filename = "timing_trace.log"
};

// Create multiple debug points to track timing changes
DebugOutputFilter_t debug_timing[3];
const char* prefixes[] = {"[T1] ", "[T2] ", "[T3] "};

for (int i = 0; i < 3; i++) {
    timing_config.prefix = prefixes[i];
    timing_config.append_mode = (i > 0);  // Append after first
    debug_output_filter_init(&debug_timing[i], &timing_config);
}
```

### 4. Debugging Binary Protocols

**Problem**: Need to inspect binary data from hardware/network.

**Solution**: Use hex or binary output format.

```c
// For examining binary protocol data
DebugOutputConfig_t binary_config = {
    .prefix = "[BINARY] ",
    .show_metadata = true,
    .show_samples = true,
    .max_samples_per_batch = 16,  // One line of hex dump
    .format = DEBUG_FMT_HEX,      // Or DEBUG_FMT_BINARY
    .flush_after_print = true
};

// For bit-level debugging
binary_config.format = DEBUG_FMT_BINARY;
binary_config.max_samples_per_batch = 4;  // Binary is verbose
```

### 5. Performance Profiling

**Problem**: Need to identify bottlenecks without affecting performance.

**Solution**: Log only metadata at intervals.

```c
// Minimal overhead profiling
DebugOutputConfig_t profile_config = {
    .prefix = "[PROFILE] ",
    .show_metadata = true,
    .show_samples = false,
    .filename = "/tmp/pipeline_profile.log",
    .flush_after_print = false  // Batch writes for performance
};

// Only log every Nth batch to reduce overhead
static int batch_counter = 0;
void* profiling_worker(void* arg) {
    // ... normal processing ...
    
    if (++batch_counter % 100 == 0) {
        // Enable debug output temporarily
        debug_filter.config.show_metadata = true;
    } else {
        debug_filter.config.show_metadata = false;
    }
}
```

### 6. Debugging Multi-Channel Data

**Problem**: Need to inspect specific channels in multi-channel data.

**Solution**: Create custom debug configuration.

```c
// For multi-channel data, show limited samples per channel
DebugOutputConfig_t multichannel_config = {
    .prefix = "[MULTICHAN] ",
    .show_metadata = true,
    .show_samples = true,
    .max_samples_per_batch = n_channels * 3,  // 3 samples per channel
    .format = DEBUG_FMT_SCIENTIFIC,
    .flush_after_print = true
};

// Add channel labels in processing
void label_channels(DebugOutputFilter_t* debug, int n_channels) {
    fprintf(debug->output_file, "Channels: ");
    for (int i = 0; i < n_channels; i++) {
        fprintf(debug->output_file, "CH%d ", i);
    }
    fprintf(debug->output_file, "\n");
}
```

### 7. Continuous Monitoring

**Problem**: Need to monitor long-running pipeline without filling disk.

**Solution**: Rotate log files or use ring buffer pattern.

```c
// Basic rotation by using append mode and external rotation
DebugOutputConfig_t monitor_config = {
    .prefix = "[MONITOR] ",
    .show_metadata = true,
    .show_samples = false,
    .filename = "/var/log/pipeline_monitor.log",
    .append_mode = true,
    .flush_after_print = true
};

// Use with logrotate or implement rotation:
void rotate_debug_log(DebugOutputFilter_t* debug, size_t max_size) {
    struct stat st;
    if (stat(debug->config.filename, &st) == 0) {
        if (st.st_size > max_size) {
            // Close current file
            if (debug->output_file != stdout) {
                fclose(debug->output_file);
            }
            
            // Rotate files
            char backup[256];
            snprintf(backup, sizeof(backup), "%s.old", debug->config.filename);
            rename(debug->config.filename, backup);
            
            // Reopen
            debug->output_file = fopen(debug->config.filename, "w");
        }
    }
}
```

## Integration Patterns

### Conditional Debug Output

```c
// Enable debug output only when needed
typedef struct {
    DebugOutputFilter_t debug;
    bool debug_enabled;
} ConditionalDebug_t;

// In your application
ConditionalDebug_t* dbg = create_conditional_debug();
if (getenv("BPIPE_DEBUG")) {
    dbg->debug_enabled = true;
}

// In pipeline setup
if (dbg->debug_enabled) {
    bp_connect(&source, 0, &dbg->debug.base, 0);
    bp_connect(&dbg->debug.base, 0, &next_filter, 0);
} else {
    bp_connect(&source, 0, &next_filter, 0);
}
```

### Debug Levels

```c
typedef enum {
    DEBUG_LEVEL_NONE = 0,
    DEBUG_LEVEL_METADATA = 1,
    DEBUG_LEVEL_SAMPLES = 2,
    DEBUG_LEVEL_VERBOSE = 3
} DebugLevel_t;

void configure_debug_output(DebugOutputFilter_t* debug, DebugLevel_t level) {
    switch (level) {
    case DEBUG_LEVEL_NONE:
        debug->config.show_metadata = false;
        debug->config.show_samples = false;
        break;
    case DEBUG_LEVEL_METADATA:
        debug->config.show_metadata = true;
        debug->config.show_samples = false;
        break;
    case DEBUG_LEVEL_SAMPLES:
        debug->config.show_metadata = true;
        debug->config.show_samples = true;
        debug->config.max_samples_per_batch = 5;
        break;
    case DEBUG_LEVEL_VERBOSE:
        debug->config.show_metadata = true;
        debug->config.show_samples = true;
        debug->config.max_samples_per_batch = -1;  // All samples
        break;
    }
}
```

## Tips and Best Practices

1. **Start Simple**: Begin with metadata only (`show_samples = false`) to understand data flow.

2. **Limit Output**: Always set `max_samples_per_batch` when debugging high-frequency data.

3. **Use Prefixes**: Clear prefixes help identify which debug point generated each line.

4. **File vs Console**: Use files for long runs, console for interactive debugging.

5. **Format Selection**:
   - `DEBUG_FMT_DECIMAL`: General purpose, human readable
   - `DEBUG_FMT_SCIENTIFIC`: Best for small or large floating point values
   - `DEBUG_FMT_HEX`: Debugging binary protocols, bit patterns
   - `DEBUG_FMT_BINARY`: Bit-level debugging, very verbose

6. **Performance Impact**: 
   - Minimal when `show_metadata = false` and `show_samples = false`
   - File I/O with `flush_after_print = true` has highest impact
   - Consider sampling (only debug every Nth batch) for production debugging

7. **Cleanup**: Always remove debug filters from production code unless conditionally disabled.

## Example Output

```
[SOURCE] [Batch t=0ns, period=1000000ns, samples=128, type=FLOAT]
[SOURCE]   [0] 0.000000
[SOURCE]   [1] 0.100000
[SOURCE]   [2] 0.200000
[SOURCE]   [3] 0.300000
[SOURCE]   [4] 0.400000
[SOURCE]   ... (123 more samples)
[PROCESS] [Batch t=0ns, period=1000000ns, samples=128, type=FLOAT]
[SINK] [Batch t=0ns, period=1000000ns, samples=128, type=FLOAT]
[SINK] [Stream completed]
```