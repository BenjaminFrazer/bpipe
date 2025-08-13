# Pipeline Filter

## Overview

The `Pipeline_t` filter is a container that encapsulates a directed acyclic graph (DAG) of interconnected filters, presenting them as a single filter with a unified interface. This enables modular, reusable processing topologies while maintaining the simplicity of bpipe2's filter model.

**Important**: A **root pipeline** (one with no external connections) must contain at least one source filter to generate data. Pipelines without source filters can only exist as nested components within other pipelines.

## Key Concepts

### Automatic Lifecycle Management

**IMPORTANT**: When you start a pipeline with `filt_start(&pipeline.base)`, it **automatically starts all internal filters** that were provided in the `filters` array during initialization. Similarly, `filt_stop(&pipeline.base)` automatically stops all internal filters in reverse order.

You only need to manually start/stop:
- **External source filters** that feed data INTO the pipeline
- **External sink filters** that receive data FROM the pipeline
- Any other filters that are connected to but not part of the pipeline

### Pipeline as a Filter

A pipeline inherits from `Filter_t` and behaves exactly like any other filter:
- Has input and output buffers
- Can be connected to other filters
- Uses standard `filt_start()`, `filt_stop()`, `filt_deinit()` operations
- Participates in property validation

### Buffer Configuration (Zero-Copy)

**IMPORTANT**: The pipeline's `buff_config` is effectively ignored. The pipeline uses a zero-copy optimization where it shares the input buffer of its designated input filter rather than maintaining its own buffer. This means:
- The `buff_config` parameter is required by `filt_init()` but immediately discarded
- The actual buffer characteristics come from the internal input filter
- You can pass a minimal/dummy buffer config since it won't be used

## Usage

### Basic Pipeline Setup

```c
#include "pipeline.h"

// 1. Create and initialize internal filters
Map_filt_t gain, offset;
CHECK_ERR(map_init(&gain, multiply_by_2_config));
CHECK_ERR(map_init(&offset, add_10_config));

// 2. Define filter array (these will be managed by the pipeline)
Filter_t* filters[] = {
    &gain.base,
    &offset.base
};

// 3. Define internal connections
Connection_t connections[] = {
    {&gain.base, 0, &offset.base, 0}  // gain output -> offset input
};

// 4. Configure pipeline
Pipeline_config_t config = {
    .name = "gain_offset_pipeline",
    .buff_config = {0},  // Ignored - pipeline shares input filter's buffer
    .timeout_us = 1000000,
    .filters = filters,
    .n_filters = 2,
    .connections = connections,
    .n_connections = 1,
    .input_filter = &gain.base,    // Pipeline input goes to gain filter
    .input_port = 0,
    .output_filter = &offset.base,  // Pipeline output comes from offset filter
    .output_port = 0
};

// 5. Initialize pipeline
Pipeline_t pipeline;
CHECK_ERR(pipeline_init(&pipeline, config));
```

### Connecting and Running

```c
// Create external filters (not part of pipeline)
SignalGenerator_t source;
CSVSink_t sink;
CHECK_ERR(signal_generator_init(&source, source_config));
CHECK_ERR(csv_sink_init(&sink, sink_config));

// Connect: source -> pipeline -> sink
CHECK_ERR(filt_sink_connect(&source.base, 0, pipeline.base.input_buffers[0]));
CHECK_ERR(filt_sink_connect(&pipeline.base, 0, sink.base.input_buffers[0]));

// Start buffers
CHECK_ERR(bb_start(pipeline.base.input_buffers[0]));
CHECK_ERR(bb_start(sink.base.input_buffers[0]));

// Start filters - pipeline starts its internal filters automatically!
CHECK_ERR(filt_start(&source.base));     // Start external source
CHECK_ERR(filt_start(&pipeline.base));   // Starts gain and offset automatically
CHECK_ERR(filt_start(&sink.base));       // Start external sink

// ... processing happens ...

// Stop filters - pipeline stops its internal filters automatically!
CHECK_ERR(filt_stop(&sink.base));        // Stop external sink
CHECK_ERR(filt_stop(&pipeline.base));    // Stops gain and offset automatically
CHECK_ERR(filt_stop(&source.base));      // Stop external source

// Clean up
CHECK_ERR(filt_deinit(&pipeline.base));  // Also deinits internal filters
CHECK_ERR(filt_deinit(&source.base));
CHECK_ERR(filt_deinit(&sink.base));
```

## Complex Topologies

Pipelines support arbitrary DAG topologies with multiple branches:

```c
// Example: Stereo processing with separate high/low frequency paths
//
//                ┌─→ HighPass → Compressor ─┐
//     Splitter ──┤                          ├─→ Mixer
//                └─→ LowPass  → Amplifier ──┘

Tee_t splitter;           // 1 input, 2 outputs
HighPass_t highpass;      
LowPass_t lowpass;
Compressor_t compressor;
Amplifier_t amplifier;
Mixer_t mixer;            // 2 inputs, 1 output

// Initialize all filters...

Filter_t* filters[] = {
    &splitter.base, &highpass.base, &lowpass.base,
    &compressor.base, &amplifier.base, &mixer.base
};

Connection_t connections[] = {
    {&splitter.base, 0, &highpass.base, 0},     // Upper path
    {&highpass.base, 0, &compressor.base, 0},
    {&compressor.base, 0, &mixer.base, 0},
    
    {&splitter.base, 1, &lowpass.base, 0},      // Lower path
    {&lowpass.base, 0, &amplifier.base, 0},
    {&amplifier.base, 0, &mixer.base, 1}
};

Pipeline_config_t config = {
    .name = "stereo_processor",
    .filters = filters,
    .n_filters = 6,
    .connections = connections,
    .n_connections = 6,
    .input_filter = &splitter.base,
    .output_filter = &mixer.base,
    // ... other config ...
};
```

## Property Validation

Pipelines participate in property validation:
- The pipeline validates internal connections during `pipeline_init()`
- Full pipeline validation occurs during `filt_start()` before starting internal filters
- Root pipelines (no external connections) must contain at least one source filter or validation fails
- Nested pipelines may consist entirely of transform filters
- Validation errors are reported with clear context about which filters failed

## Common Pitfalls

### 1. Starting Internal Filters Manually
```c
// WRONG - Pipeline manages these automatically
CHECK_ERR(filt_start(&gain.base));
CHECK_ERR(filt_start(&offset.base));
CHECK_ERR(filt_start(&pipeline.base));

// CORRECT - Only start the pipeline
CHECK_ERR(filt_start(&pipeline.base));  // Starts gain and offset automatically
```

### 2. Forgetting External Filters
```c
// WRONG - Forgot to start external source
CHECK_ERR(filt_start(&pipeline.base));

// CORRECT - Start external filters too
CHECK_ERR(filt_start(&source.base));    // External source
CHECK_ERR(filt_start(&pipeline.base));  // Pipeline and its internals
```

### 3. Creating Root Pipeline Without Sources
```c
// WRONG - Root pipeline with no source filters
Pipeline[Tee -> Map1, Map2]  // Will fail validation!

// CORRECT - Include source in pipeline
Pipeline[SignalGen -> Tee -> Map1, Map2]

// OR - Use as nested pipeline component
OuterPipeline[SignalGen -> InnerPipeline[Tee -> Map1, Map2]]
```

### 4. Wrong Cleanup Order
```c
// WRONG - Deinit pipeline before stopping
CHECK_ERR(filt_deinit(&pipeline.base));
CHECK_ERR(filt_stop(&pipeline.base));

// CORRECT - Stop first, then deinit
CHECK_ERR(filt_stop(&pipeline.base));
CHECK_ERR(filt_deinit(&pipeline.base));
```
## See Also

- [Core Data Model](../architecture/core_data_model.md) - Understanding filter architecture
- [Filter Development Guide](filter_development_guide.md) - Creating custom filters (same directory)
- [Property Validation Spec](../reference/property_validation_spec.md) - How validation works in pipelines
