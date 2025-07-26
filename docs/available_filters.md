# Available Filters

This document provides a reference for all implemented filters in the bpipe2 framework.

## Source Filters

### CSV Source (`csv_source.h`)

Reads time-series data from CSV files with automatic timing detection and flexible column mapping.

**Configuration:**
```c
typedef struct _CsvSource_config_t {
    const char* name;
    const char* file_path;
    
    char delimiter;              // Default: ','
    bool has_header;            // Whether first line contains column names
    const char* ts_column_name; // Name of timestamp column
    const char* data_column_names[BP_CSV_MAX_COLUMNS]; // NULL-terminated array
    
    bool detect_regular_timing;  // Auto-detect regular vs irregular data
    uint64_t regular_threshold_ns; // Timing tolerance (default: 1000ns)
    
    SampleDtype_t output_dtype;  // Output data type (DTYPE_FLOAT, DTYPE_I32, DTYPE_U32)
    size_t batch_size;          // Must be power of 2
    size_t ring_capacity;       // Must be power of 2
    
    bool loop;                  // Loop file when EOF reached
    bool skip_invalid;          // Skip rows with parsing errors
    long timeout_us;
} CsvSource_config_t;
```

**Features:**
- Automatic regular/irregular timing detection
- Flexible column mapping by name
- Multi-channel data support (up to 64 columns)
- Line length validation (4096 char limit)
- Loop mode for continuous replay
- Invalid row skipping option
- Metrics tracking via get_stats operation

**Example:**
```c
CsvSource_t source;
CsvSource_config_t config = {
    .name = "sensor_data",
    .file_path = "data/sensors.csv",
    .delimiter = ',',
    .has_header = true,
    .ts_column_name = "timestamp_ns",
    .data_column_names = {"accel_x", "accel_y", "accel_z", "temperature", NULL},
    .detect_regular_timing = true,
    .regular_threshold_ns = 10000,  // 10μs tolerance
    .output_dtype = DTYPE_FLOAT,
    .batch_size = 128,
    .ring_capacity = 1024,
    .timeout_us = 1000000  // 1 second
};

Bp_EC err = csvsource_init(&source, config);
```

### Signal Generator (`signal_generator.h`)

Generates synthetic waveforms for testing and simulation.

**Waveform Types:**
- Sine wave
- Square wave
- Triangle wave
- Sawtooth wave
- White noise
- Custom function

## Processing Filters

### Map Filter (`map.h`)

Applies element-wise transformations to data batches.

**Built-in Functions:**
- Scale
- Offset
- Square root
- Custom function pointer

### Sample Aligner (`sample_aligner.h`)

Aligns samples from multiple inputs based on timestamps.

**Features:**
- Exact timestamp matching
- Interpolation modes
- Configurable tolerance

### Batch Matcher (`batch_matcher.h`)

Synchronizes batches from multiple inputs.

## Sink Filters

### CSV Sink (`csv_sink.h`)

Exports data to CSV files with configurable formatting.

**Features:**
- Custom column names
- Timestamp formatting options
- Append mode support
- Buffered writing for performance

## Utility Filters

### Tee Filter (`tee.h`)

Duplicates input to multiple outputs.

**Features:**
- Zero-copy for first output
- Configurable number of outputs
- Independent output buffering

### Debug Output Filter (`debug_output_filter.h`)

A passthrough filter that prints batch metadata and sample data for debugging purposes.

**Configuration:**
```c
typedef struct {
    const char* prefix;           // Prefix for output lines (default: "DEBUG: ")
    bool show_metadata;          // Print batch metadata
    bool show_samples;           // Print sample values
    int max_samples_per_batch;   // Limit samples printed (-1 for all)
    DebugOutputFormat format;    // Output format (decimal, hex, binary, scientific)
    bool flush_after_print;      // Flush output after each batch
    const char* filename;        // Output file (NULL for stdout)
    bool append_mode;           // Append to file instead of overwrite
} DebugOutputConfig_t;
```

**Output Formats:**
- `DEBUG_FMT_DECIMAL`: Standard decimal notation
- `DEBUG_FMT_HEX`: Hexadecimal (0x format)
- `DEBUG_FMT_BINARY`: Binary (0b format)
- `DEBUG_FMT_SCIENTIFIC`: Scientific notation (for floats)

**Features:**
- Zero-overhead when printing is disabled
- Thread-safe file operations
- Supports all data types (FLOAT, I32, U32)
- Configurable sample limiting to prevent output flooding
- Batch metadata display (timestamp, period, sample count, type)
- Stream completion tracking

**Example:**
```c
DebugOutputFilter_t debug;
DebugOutputConfig_t config = {
    .prefix = "[PIPELINE] ",
    .show_metadata = true,
    .show_samples = true,
    .max_samples_per_batch = 10,  // Only show first 10 samples
    .format = DEBUG_FMT_DECIMAL,
    .flush_after_print = true,
    .filename = "debug_trace.log",
    .append_mode = false
};

Bp_EC err = debug_output_filter_init(&debug, &config);

// Insert into pipeline for debugging
// Source -> Processing -> DebugOutput -> Sink
```

For detailed examples and debugging scenarios, see [Debug Output Filter Examples](debug_output_filter_examples.md).

## Filter Operations

All filters support the standard operations interface:

- **start/stop**: Control filter execution
- **describe**: Get human-readable filter description
- **get_stats**: Retrieve processing metrics
- **get_health**: Check filter status
- **flush**: Force processing of pending data
- **drain**: Wait for all data to be processed
- **reset**: Reset filter to initial state

## Creating Custom Filters

See the [Filter Implementation Guide](filter_implementation_guide.md) for details on creating custom filters.