# Pipeline Filter Design Specification

## Overview

A Pipeline filter is a container filter type that encapsulates a directed acyclic graph (DAG) of interconnected filters, presenting them as a single filter with a unified interface. This enables modular, reusable processing topologies while maintaining consistency with bpipe2's simple, direct API philosophy.

## Motivation

1. **Complex Signal Processing**: Support multi-branch topologies (stereo processing, frequency domain analysis, beamforming)
2. **Encapsulation**: Hide internal complexity behind a simple filter interface
3. **Debuggability**: String-based filter names for clear error messages and topology understanding
4. **Lifecycle Management**: Automatically handle startup/shutdown of internal filter graphs
5. **Consistency**: Pipeline filters behave exactly like any other bpipe2 filter
6. **Testability**: Test complex processing graphs as single units while still testing components individually

## Design

### Core Structure

```c
typedef struct _Pipeline_t {
    Filter_t base;  // MUST be first member - enables standard filter interface
    
    // Filter management
    Filter_t** filters;      // Array of filter pointers
    size_t n_filters;
    
    // Connection specification (direct pointer references)
    struct {
        Filter_t* from_filter;
        size_t from_port;
        Filter_t* to_filter; 
        size_t to_port;
    } *connections;
    size_t n_connections;
    
    // External interface mapping (direct pointers)
    Filter_t* input_filter;   // Which filter provides pipeline input
    size_t input_port;        // Which port of that filter
    Filter_t* output_filter;  // Which filter provides pipeline output  
    size_t output_port;       // Which port of that filter
    
} Pipeline_t;
```

**Key Design Principles:**
- **DAG support**: Full directed acyclic graph topologies supported
- **Direct pointer references**: No redundant state, compile-time validation
- **Simple configuration**: Single config struct, no multi-step construction
- **Standard error handling**: Use existing bpipe2 patterns (first error wins)
- **Standard interface**: Pipeline inherits Filter_t, behaves like any other filter

### Filter Type

Add to `CORE_FILT_T` enum:
```c
FILT_T_PIPELINE,  /* Container for filter DAGs */
```

## API Design

### Pipeline Construction

```c
// Connection specification (direct pointer references)
typedef struct {
    Filter_t* from_filter;     // Source filter pointer
    size_t from_port;          // Source port
    Filter_t* to_filter;       // Destination filter pointer  
    size_t to_port;            // Destination port
} Connection_t;

// Configuration structure (follows bpipe2 patterns)
typedef struct _Pipeline_config_t {
    const char* name;
    Batch_buffer_config_t buff_config;  // For pipeline's input buffer
    long timeout_us;
    
    // Filter topology (direct references)
    Filter_t** filters;              // Array of pre-initialized filter pointers
    size_t n_filters;
    Connection_t* connections;       // Array of connections
    size_t n_connections;
    
    // External interface (direct pointers)
    Filter_t* input_filter;          // Which filter to expose as input
    size_t input_port;               // Which port (default: 0)
    Filter_t* output_filter;         // Which filter to expose as output
    size_t output_port;              // Which port (default: 0)
} Pipeline_config_t;

// Standard bpipe2 initialization pattern
Bp_EC pipeline_init(Pipeline_t* pipe, Pipeline_config_t config);

// Standard filter lifecycle (inherited from Filter_t)
// filt_start(), filt_stop(), filt_deinit() work automatically
```

**Key Design Features:**
- **Single init function**: No multi-step construction process
- **Pre-initialized filters**: Users initialize filters, then pass to pipeline
- **Direct pointer connections**: Compile-time validation, no redundant state
- **Standard lifecycle**: Uses existing filt_start/stop/deinit infrastructure

**Advantages of Pointer-Based Connections:**
- **No redundant state**: Eliminates duplication between filter names and connection strings
- **Compile-time validation**: Typos in filter references caught at compile time
- **Memory efficient**: 32 bytes per connection vs 80 bytes for string-based
- **Simpler implementation**: No string lookup or comparison functions needed
- **Aligned with bpipe2 principles**: Direct C references, "simple and direct"

### Usage Example - DC Offset Removal Pipeline

```c
// Create component filters
Tee_t splitter;
LowPassFilter_t dc_estimator;  
ElementwiseSubtract_t subtractor;

CHECK_ERR(tee_init(&splitter, tee_config_2_outputs));
CHECK_ERR(lowpass_init(&dc_estimator, dc_config));
CHECK_ERR(elementwise_subtract_init(&subtractor, subtract_config));

// Create filter array (direct pointers)
Filter_t* filters[] = {
    &splitter.base,
    &dc_estimator.base,
    &subtractor.base
};

// Define connections (direct pointer references - no string lookup needed)
Connection_t connections[] = {
    {&splitter.base, 0, &dc_estimator.base, 0},     // splitter[0] -> dc_estimator (DC estimation)
    {&dc_estimator.base, 0, &subtractor.base, 1},   // dc_estimator -> subtractor[1] (DC estimate)
    {&splitter.base, 1, &subtractor.base, 0}        // splitter[1] -> subtractor[0] (original signal)
};

// Pipeline configuration
Pipeline_config_t config = {
    .name = "dc_offset_removal",
    .buff_config = {.dtype = DTYPE_FLOAT, .batch_capacity_expo = 6, 
                    .ring_capacity_expo = 4, .overflow_behaviour = OVERFLOW_BLOCK},
    .timeout_us = 100000,
    .filters = filters,
    .n_filters = 3,
    .connections = connections,
    .n_connections = 3,
    .input_filter = &splitter.base,      // Pipeline input goes to splitter
    .input_port = 0,
    .output_filter = &subtractor.base,   // Pipeline output comes from subtractor
    .output_port = 0
};

// Single initialization call
Pipeline_t dc_pipeline;
CHECK_ERR(pipeline_init(&dc_pipeline, config));

// Use exactly like any other filter - no difference!
CHECK_ERR(filt_sink_connect(&source, 0, &dc_pipeline.base.input_buffers[0]));
CHECK_ERR(filt_sink_connect(&dc_pipeline.base, 0, &sink.base.input_buffers[0]));
CHECK_ERR(filt_start(&dc_pipeline.base));  // Starts entire pipeline
```

### Usage Example - Stereo Audio Processor

```c
// More complex example: stereo processing with parallel paths
ChannelSplitter_t splitter;
Compressor_t left_comp, right_comp;
EQ_t left_eq, right_eq;
ChannelCombiner_t combiner;

// Initialize all filters...
CHECK_ERR(channel_splitter_init(&splitter, splitter_config));
CHECK_ERR(compressor_init(&left_comp, comp_config));
CHECK_ERR(compressor_init(&right_comp, comp_config));
CHECK_ERR(eq_init(&left_eq, eq_config));
CHECK_ERR(eq_init(&right_eq, eq_config));
CHECK_ERR(channel_combiner_init(&combiner, combiner_config));

// Define the stereo processing topology (direct pointers)
Filter_t* stereo_filters[] = {
    &splitter.base,
    &left_comp.base, 
    &right_comp.base,
    &left_eq.base,
    &right_eq.base,
    &combiner.base
};

Connection_t stereo_connections[] = {
    {&splitter.base, 0, &left_comp.base, 0},     // Left channel: split -> compress -> EQ
    {&left_comp.base, 0, &left_eq.base, 0},
    {&splitter.base, 1, &right_comp.base, 0},    // Right channel: split -> compress -> EQ  
    {&right_comp.base, 0, &right_eq.base, 0},
    {&left_eq.base, 0, &combiner.base, 0},       // Recombine processed channels
    {&right_eq.base, 0, &combiner.base, 1}
};

Pipeline_config_t stereo_config = {
    .name = "stereo_processor",
    .buff_config = stereo_buffer_config,
    .timeout_us = 100000,
    .filters = stereo_filters,
    .n_filters = 6,
    .connections = stereo_connections,
    .n_connections = 6,
    .input_filter = &splitter.base,
    .input_port = 0,
    .output_filter = &combiner.base, 
    .output_port = 0
};

Pipeline_t stereo_processor;
CHECK_ERR(pipeline_init(&stereo_processor, stereo_config));
```

## Implementation Details

### Initialization

```c
Bp_EC pipeline_init(Pipeline_t* pipe, Pipeline_config_t config) {
    if (!pipe || !config.filters || config.n_filters == 0) return Bp_EC_NULL_PTR;
    if (!config.input_filter || !config.output_filter) return Bp_EC_NULL_PTR;
    
    // Initialize base filter 
    Core_filt_config_t core_config = {
        .name = config.name,
        .filt_type = FILT_T_PIPELINE,
        .size = sizeof(Pipeline_t),
        .n_inputs = 1,               // Pipeline has single input
        .max_supported_sinks = 1,    // Pipeline has single output
        .buff_config = config.buff_config,
        .timeout_us = config.timeout_us,
        .worker = NULL  // No pipeline worker - uses existing filter workers
    };
    
    TRY(filt_init(&pipe->base, core_config));
    
    // Copy filter pointers (no string duplication needed)
    pipe->filters = malloc(config.n_filters * sizeof(Filter_t*));
    if (!pipe->filters) return Bp_EC_ALLOC;
    
    memcpy(pipe->filters, config.filters, config.n_filters * sizeof(Filter_t*));
    pipe->n_filters = config.n_filters;
    
    // Validate all filters are in our filter list (compile-time safety net)
    for (size_t i = 0; i < config.n_connections; i++) {
        if (!pipeline_contains_filter(pipe, config.connections[i].from_filter) ||
            !pipeline_contains_filter(pipe, config.connections[i].to_filter)) {
            return Bp_EC_INVALID_ARG;
        }
    }
    
    // Copy connections (direct pointer references)
    pipe->connections = malloc(config.n_connections * sizeof(*pipe->connections));
    if (!pipe->connections) return Bp_EC_ALLOC;
    
    memcpy(pipe->connections, config.connections, 
           config.n_connections * sizeof(*pipe->connections));
    pipe->n_connections = config.n_connections;
    
    // Create internal connections using filt_sink_connect
    for (size_t i = 0; i < config.n_connections; i++) {
        Connection_t* conn = &pipe->connections[i];
        TRY(filt_sink_connect(conn->from_filter, conn->from_port,
                             &conn->to_filter->input_buffers[conn->to_port]));
    }
    
    // Set up external interface (direct pointer references)
    pipe->input_filter = config.input_filter;
    pipe->input_port = config.input_port;
    pipe->output_filter = config.output_filter;
    pipe->output_port = config.output_port;
    
    // Validate external interface filters are in our pipeline
    if (!pipeline_contains_filter(pipe, config.input_filter) ||
        !pipeline_contains_filter(pipe, config.output_filter)) {
        return Bp_EC_INVALID_ARG;
    }
    
    // Share input buffer with designated input filter (zero-copy)
    pipe->base.input_buffers[0] = &config.input_filter->input_buffers[config.input_port];
    
    return Bp_EC_OK;
}

// Helper function to validate filter is in pipeline
static bool pipeline_contains_filter(Pipeline_t* pipe, Filter_t* filter) {
    for (size_t i = 0; i < pipe->n_filters; i++) {
        if (pipe->filters[i] == filter) {
            return true;
        }
    }
    return false;
}
```

### Lifecycle Management

```c
// Pipeline leverages existing filter lifecycle management
Bp_EC pipeline_start(Pipeline_t* pipe) {
    // Start internal filters (order doesn't matter - they're already connected)
    for (size_t i = 0; i < pipe->n_filters; i++) {
        TRY(filt_start(pipe->filters[i]));
    }
    
    // Pipeline itself has no worker thread - internal filters do the work
    atomic_store(&pipe->base.running, true);
    return Bp_EC_OK;
}

Bp_EC pipeline_stop(Pipeline_t* pipe) {
    // Signal stop
    atomic_store(&pipe->base.running, false);
    
    // Stop internal filters in reverse order (for clean shutdown)
    for (int i = pipe->n_filters - 1; i >= 0; i--) {
        filt_stop(pipe->filters[i]);
    }
    
    return Bp_EC_OK;
}

Bp_EC pipeline_deinit(Pipeline_t* pipe) {
    if (pipe->filters) {
        // Clean up internal filters
        for (size_t i = 0; i < pipe->n_filters; i++) {
            filt_deinit(pipe->filters[i]);
        }
        free(pipe->filters);
        pipe->filters = NULL;
    }
    
    if (pipe->connections) {
        free(pipe->connections);
        pipe->connections = NULL;
    }
    
    return filt_deinit(&pipe->base);
}

// Enhanced describe function for debugging
static Bp_EC pipeline_describe(Filter_t* self, char* buffer, size_t size) {
    Pipeline_t* pipe = (Pipeline_t*)self;
    
    size_t written = snprintf(buffer, size, 
        "Pipeline '%s': %zu filters, %zu connections\n",
        self->name, pipe->n_filters, pipe->n_connections);
    
    // Show topology (use filter names from pointers)
    written += snprintf(buffer + written, size - written,
                       "Input: %s[%zu] -> Output: %s[%zu]\n",
                       pipe->input_filter->name, pipe->input_port,
                       pipe->output_filter->name, pipe->output_port);
    
    // Show filter states with names (get names from filter pointers)
    for (size_t i = 0; i < pipe->n_filters && written < size; i++) {
        Filter_t* f = pipe->filters[i];
        const char* status = atomic_load(&f->running) ? "running" : "stopped";
        const char* error = f->worker_err_info.ec == Bp_EC_OK ? "OK" : err_lut[f->worker_err_info.ec];
        written += snprintf(buffer + written, size - written,
                           "  %s: %s (%s)\n", 
                           f->name, status, error);
    }
    
    return Bp_EC_OK;
}
```

## Pipeline Helper Functions

To reduce boilerplate, provide common pipeline patterns:

```c
// Create a simple 2-filter linear chain
Pipeline_t* pipeline_create_chain2(const char* name, 
                                  Filter_t* first, Filter_t* second,
                                  Batch_buffer_config_t buff_config, 
                                  long timeout_us) {
    Pipeline_t* pipe = malloc(sizeof(Pipeline_t));
    if (!pipe) return NULL;
    
    Filter_t* filters[] = {first, second};
    
    Connection_t connections[] = {
        {first, 0, second, 0}
    };
    
    Pipeline_config_t config = {
        .name = name,
        .buff_config = buff_config,
        .timeout_us = timeout_us,
        .filters = filters,
        .n_filters = 2,
        .connections = connections,
        .n_connections = 1,
        .input_filter = first,
        .input_port = 0,
        .output_filter = second,
        .output_port = 0
    };
    
    if (pipeline_init(pipe, config) != Bp_EC_OK) {
        free(pipe);
        return NULL;
    }
    
    return pipe;
}

// Create DC offset removal pipeline factory
Pipeline_t* create_dc_offset_pipeline(const char* name, float alpha,
                                     Batch_buffer_config_t buff_config,
                                     long timeout_us) {
    // Create and initialize component filters
    Tee_t* splitter = malloc(sizeof(Tee_t));
    LowPassFilter_t* dc_estimator = malloc(sizeof(LowPassFilter_t));
    ElementwiseSubtract_t* subtractor = malloc(sizeof(ElementwiseSubtract_t));
    
    if (!splitter || !dc_estimator || !subtractor) return NULL;
    
    TeeConfig_t tee_config = {.name = "internal_tee", .n_outputs = 2, .buff_config = buff_config, .timeout_us = timeout_us};
    LowPassConfig_t lpf_config = {.name = "internal_lpf", .alpha = alpha, .buff_config = buff_config, .timeout_us = timeout_us};
    ElementwiseSubtractConfig_t sub_config = {.name = "internal_sub", .buff_config = buff_config, .timeout_us = timeout_us};
    
    if (tee_init(splitter, tee_config) != Bp_EC_OK ||
        lowpass_init(dc_estimator, lpf_config) != Bp_EC_OK ||
        elementwise_subtract_init(subtractor, sub_config) != Bp_EC_OK) {
        // Cleanup on failure...
        return NULL;
    }
    
    // Create pipeline
    Filter_t* filters[] = {
        &splitter->base,
        &dc_estimator->base,
        &subtractor->base
    };
    
    Connection_t connections[] = {
        {&splitter->base, 0, &dc_estimator->base, 0},     // splitter[0] -> dc_estimator
        {&dc_estimator->base, 0, &subtractor->base, 1},   // dc_estimator -> subtractor[1] (DC estimate)
        {&splitter->base, 1, &subtractor->base, 0}        // splitter[1] -> subtractor[0] (signal)
    };
    
    Pipeline_config_t config = {
        .name = name,
        .buff_config = buff_config,
        .timeout_us = timeout_us,
        .filters = filters,
        .n_filters = 3,
        .connections = connections,
        .n_connections = 3,
        .input_filter = &splitter->base,
        .input_port = 0,
        .output_filter = &subtractor->base,
        .output_port = 0
    };
    
    Pipeline_t* pipe = malloc(sizeof(Pipeline_t));
    if (!pipe || pipeline_init(pipe, config) != Bp_EC_OK) {
        free(pipe);
        return NULL;
    }
    
    return pipe;
}
```

## Implementation Plan

Two-phase implementation balancing simplicity with DAG support:

## Phase 1: Core DAG Pipeline Implementation

**Objective**: Implement full DAG pipeline functionality with standard bpipe2 patterns

**Deliverables**:
1. `Pipeline_t` struct definition in `bpipe/pipeline.h`
2. `pipeline_init()` function with DAG connection support
3. Direct pointer-based filter connections (no string lookup)
4. Standard filter lifecycle support (`filt_start`, `filt_stop`, `filt_deinit`)
5. Connection validation and error handling

**Files to Create/Modify**:
- `bpipe/pipeline.h` - Pipeline structure and API declarations
- `bpipe/pipeline.c` - Implementation with DAG support
- `bpipe/core.h` - Add `FILT_T_PIPELINE` to enum

**Success Criteria**:
- Pipeline can be initialized with arbitrary DAG topologies
- Direct pointer-based filter connections work correctly
- Standard filter lifecycle works (start/stop/deinit)
- Enhanced debugging with filter names from pointers
- Connection validation catches common errors

### Phase 1 Test Specification

```c
// Test file: tests/test_pipeline_dag.c
void test_pipeline_linear_chain(void) {
    // Create component filters
    Map_t gain_filter, offset_filter;
    CHECK_ERR(map_init(&gain_filter, multiply_by_2_config));
    CHECK_ERR(map_init(&offset_filter, add_10_config));
    
    // Create linear chain pipeline (direct pointers)
    Filter_t* filters[] = {
        &gain_filter.base,
        &offset_filter.base
    };
    
    Connection_t connections[] = {
        {&gain_filter.base, 0, &offset_filter.base, 0}
    };
    
    Pipeline_config_t config = {
        .name = "linear_chain",
        .buff_config = default_buffer_config(),
        .timeout_us = 1000000,
        .filters = filters,
        .n_filters = 2,
        .connections = connections,
        .n_connections = 1,
        .input_filter = &gain_filter.base,
        .input_port = 0,
        .output_filter = &offset_filter.base,
        .output_port = 0
    };
    
    Pipeline_t pipeline;
    CHECK_ERR(pipeline_init(&pipeline, config));
    
    // Verify structure
    TEST_ASSERT_EQUAL_STRING("linear_chain", pipeline.base.name);
    TEST_ASSERT_EQUAL(FILT_T_PIPELINE, pipeline.base.filt_type);
    TEST_ASSERT_EQUAL(2, pipeline.n_filters);
    TEST_ASSERT_EQUAL(1, pipeline.n_connections);
    TEST_ASSERT_EQUAL_PTR(&gain_filter.base, pipeline.input_filter);
    TEST_ASSERT_EQUAL_PTR(&offset_filter.base, pipeline.output_filter);
    
    pipeline_deinit(&pipeline);
}

void test_pipeline_multi_branch_dag(void) {
    // Create filters for multi-branch topology
    Tee_t splitter;
    Map_t gain1, gain2;
    Mixer_t combiner;
    
    CHECK_ERR(tee_init(&splitter, tee_config_2_outputs));
    CHECK_ERR(map_init(&gain1, multiply_by_2_config));
    CHECK_ERR(map_init(&gain2, multiply_by_3_config));
    CHECK_ERR(mixer_init(&combiner, mixer_config_2_inputs));
    
    // Define DAG topology: splitter -> [gain1, gain2] -> combiner (direct pointers)
    Filter_t* filters[] = {
        &splitter.base,
        &gain1.base,
        &gain2.base,
        &combiner.base
    };
    
    Connection_t connections[] = {
        {&splitter.base, 0, &gain1.base, 0},     // Split left channel
        {&splitter.base, 1, &gain2.base, 0},     // Split right channel
        {&gain1.base, 0, &combiner.base, 0},     // Combine processed left
        {&gain2.base, 0, &combiner.base, 1}      // Combine processed right
    };
    
    Pipeline_config_t config = {
        .name = "parallel_processor",
        .buff_config = default_buffer_config(),
        .timeout_us = 1000000,
        .filters = filters,
        .n_filters = 4,
        .connections = connections,
        .n_connections = 4,
        .input_filter = &splitter.base,
        .input_port = 0,
        .output_filter = &combiner.base,
        .output_port = 0
    };
    
    Pipeline_t pipeline;
    CHECK_ERR(pipeline_init(&pipeline, config));
    
    // Verify complex topology
    TEST_ASSERT_EQUAL(4, pipeline.n_filters);
    TEST_ASSERT_EQUAL(4, pipeline.n_connections);
    TEST_ASSERT_EQUAL_PTR(&splitter.base, pipeline.input_filter);
    TEST_ASSERT_EQUAL_PTR(&combiner.base, pipeline.output_filter);
    
    pipeline_deinit(&pipeline);
}

void test_pipeline_lifecycle_and_errors(void) {
    // Create simple pipeline for lifecycle testing
    Map_t gain_filter, offset_filter;
    CHECK_ERR(map_init(&gain_filter, multiply_by_2_config));
    CHECK_ERR(map_init(&offset_filter, add_10_config));
    
    Filter_t* filters[] = {
        &gain_filter.base,
        &offset_filter.base
    };
    
    Connection_t connections[] = {
        {&gain_filter.base, 0, &offset_filter.base, 0}
    };
    
    Pipeline_config_t config = {
        .name = "lifecycle_test",
        .buff_config = default_buffer_config(),
        .timeout_us = 1000000,
        .filters = filters,
        .n_filters = 2,
        .connections = connections,
        .n_connections = 1,
        .input_filter = &gain_filter.base,
        .input_port = 0,
        .output_filter = &offset_filter.base,
        .output_port = 0
    };
    
    Pipeline_t pipeline;
    CHECK_ERR(pipeline_init(&pipeline, config));
    
    // Test start/stop lifecycle
    CHECK_ERR(filt_start(&pipeline.base));
    TEST_ASSERT_TRUE(atomic_load(&pipeline.base.running));
    
    // Test enhanced describe function (uses filter->name from pointers)
    char buffer[1024];
    CHECK_ERR(filt_describe(&pipeline.base, buffer, sizeof(buffer)));
    TEST_ASSERT_TRUE(strstr(buffer, "lifecycle_test") != NULL);
    TEST_ASSERT_TRUE(strstr(buffer, gain_filter.base.name) != NULL);
    TEST_ASSERT_TRUE(strstr(buffer, offset_filter.base.name) != NULL);
    TEST_ASSERT_TRUE(strstr(buffer, "running") != NULL);
    
    CHECK_ERR(filt_stop(&pipeline.base));
    TEST_ASSERT_FALSE(atomic_load(&pipeline.base.running));
    
    pipeline_deinit(&pipeline);
}

void test_pipeline_connection_validation(void) {
    Map_t gain_filter;
    Map_t other_filter;  // Filter not in pipeline
    CHECK_ERR(map_init(&gain_filter, multiply_by_2_config));
    CHECK_ERR(map_init(&other_filter, multiply_by_3_config));
    
    Filter_t* filters[] = {
        &gain_filter.base
    };
    
    // Test invalid connection - filter not in pipeline
    Connection_t bad_connections[] = {
        {&gain_filter.base, 0, &other_filter.base, 0}  // other_filter not in pipeline
    };
    
    Pipeline_config_t config = {
        .name = "validation_test",
        .buff_config = default_buffer_config(),
        .timeout_us = 1000000,
        .filters = filters,
        .n_filters = 1,
        .connections = bad_connections,
        .n_connections = 1,
        .input_filter = &gain_filter.base,
        .input_port = 0,
        .output_filter = &gain_filter.base, 
        .output_port = 0
    };
    
    Pipeline_t pipeline;
    // Should fail due to invalid connection (other_filter not in pipeline)
    TEST_ASSERT_EQUAL(Bp_EC_INVALID_ARG, pipeline_init(&pipeline, config));
    
    filt_deinit(&other_filter.base);
}
```

## Phase 2: Helper Functions and Real-World Integration

**Objective**: Complete ecosystem integration and provide convenience functions for common patterns

**Deliverables**:
1. Helper functions for common topologies (`pipeline_create_chain2`, `create_dc_offset_pipeline`, etc.)
2. Integration testing with existing bpipe2 filters (signal generators, sinks, processors)
3. Performance validation against manual filter chaining
4. Documentation and examples for complex signal processing patterns
5. End-to-end test scenarios covering real-world use cases

**Dependencies**: Phase 1 complete

**Success Criteria**:
- Helper functions significantly reduce boilerplate for common patterns
- Pipeline works seamlessly with all existing bpipe2 filters
- Performance overhead is minimal (< 5% vs manual chaining)
- Complex signal processing topologies work correctly (DC removal, stereo processing, etc.)
- Full integration tests pass including error scenarios

### Phase 2 Test Specification

```c
// Test file: tests/test_pipeline_integration.c
void test_dc_offset_removal_end_to_end(void) {
    // Create DC offset removal pipeline using helper function
    Pipeline_t* dc_pipeline = create_dc_offset_pipeline("dc_removal", 0.01f,
                                                        default_buffer_config(), 
                                                        100000);
    TEST_ASSERT_NOT_NULL(dc_pipeline);
    
    // Create source with DC offset and sink
    SignalGenerator_t source;
    SignalGeneratorCfg_t src_config = {
        .name = "dc_source",
        .waveform = SINE,
        .frequency_hz = 10.0,
        .amplitude = 1.0,
        .offset = 2.5,  // Add 2.5V DC offset
        .phase_rad = 0.0,
        .period_ns = 1000000,
        .n_channels = 1,
        .max_samples = 1000,
        .buff_config = default_buffer_config(),
        .timeout_us = 100000
    };
    CHECK_ERR(signal_generator_init(&source, src_config));
    
    CsvSink_t sink;
    CsvSinkCfg_t sink_config = {
        .name = "output",
        .filename = "/tmp/dc_removal_test.csv",
        .max_samples = 1000,
        .buff_config = default_buffer_config(),
        .timeout_us = 100000
    };
    CHECK_ERR(csv_sink_init(&sink, sink_config));
    
    // Connect: source -> dc_pipeline -> sink
    CHECK_ERR(filt_sink_connect(&source.base, 0, &dc_pipeline->base.input_buffers[0]));
    CHECK_ERR(filt_sink_connect(&dc_pipeline->base, 0, &sink.base.input_buffers[0]));
    
    // Start everything
    CHECK_ERR(filt_start(&source.base));
    CHECK_ERR(filt_start(&dc_pipeline->base));  // Starts all internal filters
    CHECK_ERR(filt_start(&sink.base));
    
    // Run test
    usleep(200000);  // 200ms for DC estimation to converge
    
    // Stop everything
    CHECK_ERR(filt_stop(&source.base));
    CHECK_ERR(filt_stop(&dc_pipeline->base));
    CHECK_ERR(filt_stop(&sink.base));
    
    // Verify no errors
    CHECK_ERR(source.base.worker_err_info.ec);
    CHECK_ERR(dc_pipeline->base.worker_err_info.ec);
    CHECK_ERR(sink.base.worker_err_info.ec);
    
    // Verify DC removal worked (output should have minimal DC component)
    verify_dc_removal_effectiveness(&sink, 2.5f);
    
    // Cleanup
    filt_deinit(&source.base);
    free(dc_pipeline);  // Helper function allocated
    filt_deinit(&sink.base);
}

void test_stereo_processing_pipeline(void) {
    // Test complex stereo processing topology
    ChannelSplitter_t splitter;
    Compressor_t left_comp, right_comp;
    EQ_t left_eq, right_eq;  
    ChannelCombiner_t combiner;
    
    // Initialize all component filters...
    CHECK_ERR(channel_splitter_init(&splitter, stereo_config));
    CHECK_ERR(compressor_init(&left_comp, comp_config));
    CHECK_ERR(compressor_init(&right_comp, comp_config));
    CHECK_ERR(eq_init(&left_eq, eq_config));
    CHECK_ERR(eq_init(&right_eq, eq_config));
    CHECK_ERR(channel_combiner_init(&combiner, stereo_config));
    
    // Define stereo processing topology (direct pointers)
    Filter_t* filters[] = {
        &splitter.base,
        &left_comp.base, 
        &right_comp.base,
        &left_eq.base,
        &right_eq.base,
        &combiner.base
    };
    
    Connection_t connections[] = {
        {&splitter.base, 0, &left_comp.base, 0},
        {&left_comp.base, 0, &left_eq.base, 0},
        {&splitter.base, 1, &right_comp.base, 0},
        {&right_comp.base, 0, &right_eq.base, 0},
        {&left_eq.base, 0, &combiner.base, 0},
        {&right_eq.base, 0, &combiner.base, 1}
    };
    
    Pipeline_config_t config = {
        .name = "stereo_processor",
        .buff_config = stereo_buffer_config,
        .timeout_us = 100000,
        .filters = filters,
        .n_filters = 6,
        .connections = connections,
        .n_connections = 6,
        .input_filter = &splitter.base,
        .input_port = 0,
        .output_filter = &combiner.base,
        .output_port = 0
    };
    
    Pipeline_t stereo_pipeline;
    CHECK_ERR(pipeline_init(&stereo_pipeline, config));
    
    // Test the complex pipeline works correctly
    // (setup source/sink, run test, verify correctness...)
    
    pipeline_deinit(&stereo_pipeline);
}

void test_pipeline_performance_benchmark(void) {
    // Compare pipeline vs manual chaining performance
    
    // Manual chaining setup
    Map_t manual_gain, manual_offset;
    CHECK_ERR(map_init(&manual_gain, multiply_by_2_config));
    CHECK_ERR(map_init(&manual_offset, add_10_config));
    CHECK_ERR(filt_sink_connect(&manual_gain.base, 0, &manual_offset.base.input_buffers[0]));
    
    // Pipeline setup (direct pointers)
    Filter_t* pipeline_filters[] = {
        &manual_gain.base,
        &manual_offset.base
    };
    Connection_t pipeline_connections[] = {
        {&manual_gain.base, 0, &manual_offset.base, 0}
    };
    Pipeline_config_t config = {
        .name = "perf_test",
        .buff_config = default_buffer_config(),
        .timeout_us = 100000,
        .filters = pipeline_filters,
        .n_filters = 2,
        .connections = pipeline_connections,
        .n_connections = 1,
        .input_filter = &manual_gain.base,
        .input_port = 0,
        .output_filter = &manual_offset.base,
        .output_port = 0
    };
    
    Pipeline_t pipeline;
    CHECK_ERR(pipeline_init(&pipeline, config));
    
    // Both should have identical performance (same underlying connections)
    double manual_time = benchmark_manual_processing(&manual_gain.base);
    double pipeline_time = benchmark_pipeline_processing(&pipeline.base);
    
    // Pipeline overhead should be minimal
    double overhead = (pipeline_time - manual_time) / manual_time;
    TEST_ASSERT_LESS_THAN(0.05, overhead);  // Less than 5% overhead
    
    pipeline_deinit(&pipeline);
}
```

## Key Benefits of Middle-Ground Design

### **Consistency with bpipe2 Philosophy** 
- **Simple and direct**: Single init function, standard lifecycle
- **Focused on real needs**: DAG support for actual signal processing use cases
- **No redundant state**: Uses existing error handling and connection patterns
- **Clear interfaces**: String-based connections are readable and debuggable

### **Powerful Signal Processing**
- **Full DAG support**: Enables complex topologies (DC removal, stereo processing, beamforming)
- **Multi-branch/merge**: Split signals for parallel processing, recombine results
- **Reusable patterns**: Helper functions for common signal processing topologies
- **Industrial strength**: Handles real-world audio/RF/sensor processing needs

### **Developer Experience**
- **Readable topology**: String-based connections make complex graphs understandable
- **Excellent debugging**: Named filters in error messages and status reports
- **Familiar patterns**: Same initialization style as other bpipe2 filters
- **Progressive complexity**: Simple chains are still simple, complex DAGs are possible

### **Performance**
- **No additional worker threads**: Uses existing filter workers
- **Minimal overhead**: ~5% vs manual chaining (just connection lookup cost)
- **Zero-copy potential**: Direct buffer sharing where possible
- **Scales well**: Linear filter lookup is fast for reasonable pipeline sizes

### **Maintainability**
- **Moderate implementation**: ~400 lines vs 1000+ for over-engineered version
- **Reuses existing code**: Leverages filt_start/stop/deinit infrastructure
- **Standard error handling**: No custom aggregation, uses existing patterns
- **Clear separation**: Core functionality vs helper functions

## Trade-offs of Middle-Ground Design

### **What We Give Up (vs Over-Engineered)**
- **Builder pattern**: No fluent interface for construction
- **Dynamic reconfiguration**: No runtime filter add/remove
- **Advanced orchestration**: No topological sorting, complex scheduling
- **Rich error aggregation**: Standard bpipe2 error handling only (first error wins)
- **Template system**: No reusable pipeline patterns via templates

### **What We Gain (vs Over-Engineered)**  
- **Simplicity**: ~400 lines vs 1000+ line implementation
- **Consistency**: Same patterns as all other bpipe2 filters
- **Maintainability**: Uses existing, well-tested infrastructure
- **Performance**: Minimal overhead from simple design
- **Predictability**: No complex runtime behavior

### **What We Keep (vs Oversimplified)**
- **Full DAG support**: Complex signal processing topologies work
- **String-based identification**: Readable connections and great debugging
- **Multi-branch processing**: Enables real-world signal processing patterns
- **Helper functions**: Reduce boilerplate for common patterns

## Migration from Manual Filter Chaining

**Before** (manual multi-branch chaining):
```c
// Create filters individually
Tee_t splitter;
LowPassFilter_t dc_estimator;
ElementwiseSubtract_t subtractor;

tee_init(&splitter, tee_config);
lowpass_init(&dc_estimator, lpf_config);
elementwise_subtract_init(&subtractor, sub_config);

// Connect manually (error-prone with complex topologies)
filt_sink_connect(&source, 0, &splitter.base.input_buffers[0]);
filt_sink_connect(&splitter.base, 0, &dc_estimator.base.input_buffers[0]);  // splitter[0]
filt_sink_connect(&splitter.base, 1, &subtractor.base.input_buffers[0]);    // splitter[1] 
filt_sink_connect(&dc_estimator.base, 0, &subtractor.base.input_buffers[1]); // DC estimate
filt_sink_connect(&subtractor.base, 0, &sink.base.input_buffers[0]);

// Start individually (order matters)
filt_start(&splitter.base);
filt_start(&dc_estimator.base);
filt_start(&subtractor.base);
```

**After** (pipeline with DAG support):  
```c
// Create filters
Tee_t splitter;
LowPassFilter_t dc_estimator;  
ElementwiseSubtract_t subtractor;

tee_init(&splitter, tee_config);
lowpass_init(&dc_estimator, lpf_config);
elementwise_subtract_init(&subtractor, sub_config);

// Create pipeline with clear, readable topology
NamedFilter_t filters[] = {
    {"splitter", &splitter.base},
    {"dc_lpf", &dc_estimator.base},
    {"subtract", &subtractor.base}
};

Connection_t connections[] = {
    {"splitter", 0, "dc_lpf", 0},      // DC estimation path
    {"dc_lpf", 0, "subtract", 1},      // DC estimate to subtractor
    {"splitter", 1, "subtract", 0}     // Original signal to subtractor
};

Pipeline_config_t config = {
    .name = "dc_removal",
    .buff_config = default_buffer_config(),
    .timeout_us = 100000,
    .filters = filters,
    .n_filters = 3,
    .connections = connections,
    .n_connections = 3,
    .input_filter = "splitter",
    .input_port = 0,
    .output_filter = "subtract",
    .output_port = 0
};

Pipeline_t dc_pipeline;
pipeline_init(&dc_pipeline, config);

// Connect and start as single unit
filt_sink_connect(&source, 0, &dc_pipeline.base.input_buffers[0]);
filt_sink_connect(&dc_pipeline.base, 0, &sink.base.input_buffers[0]);
filt_start(&dc_pipeline.base);  // Starts all internal filters automatically
```

## Conclusion

This middle-ground pipeline design provides **real signal processing power without over-engineering**. It supports the complex DAG topologies needed for actual audio/RF/sensor processing while maintaining consistency with bpipe2's core philosophy of simplicity and directness.

**Key achievements:**
- **Full DAG support** for multi-branch signal processing topologies
- **Direct pointer connections** for compile-time safety and memory efficiency
- **Standard bpipe2 patterns** - pipelines behave exactly like any other filter
- **Moderate complexity** - ~400 lines vs 1000+ for over-engineered approaches
- **Real-world capable** - handles DC removal, stereo processing, beamforming, etc.

**Pipeline filters are powerful yet familiar** - they support complex internal topologies while presenting the same simple interface as any other bpipe2 filter. This enables sophisticated signal processing applications without sacrificing the framework's core principles of simplicity and maintainability.