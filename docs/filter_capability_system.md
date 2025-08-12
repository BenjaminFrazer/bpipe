# Filter Capability and Requirements System

## Implementation Status

**Implemented (Phases 0-3)**:
- ✅ Basic property table structure and operations
- ✅ Input constraint declarations via `prop_append_constraint()`
- ✅ Output behavior declarations via `prop_append_behavior()`
- ✅ SET and PRESERVE behavior operators
- ✅ Basic constraint operators (EXISTS, EQ, GTE, LTE)
- ✅ Helper functions for buffer-based filters
- ✅ Pipeline-wide validation (`pipeline_validate_properties()`)
- ✅ Property propagation through `prop_propagate()` with multi-input support
- ✅ Multi-input alignment validation (`prop_validate_multi_input_alignment()`)
- ✅ Input property tables (input_properties[MAX_INPUTS])
- ✅ Validation automatically called during `pipeline_start()`
- ✅ Source filters using behaviors and prop_propagate

**Not Yet Implemented**:
- ❌ Multi-output filter support (no MAX_OUTPUTS, single output_properties only)
- ❌ Nested pipeline external inputs (parameter exists but not used)
- ❌ Full topological DAG traversal (linear validation only)
- ❌ Output port parameter in prop_propagate (always uses port 0)

**Connection Validation Status**:
- `filt_connect()` currently only establishes DAG edges, no validation
- Full validation is deferred to the pipeline validation phase (not yet implemented)

## Overview

The property system allows filters to explicitly declare their input requirements and output capabilities, enabling automatic validation of filter connections and preventing runtime failures from incompatible configurations.

## Problem Statement

Without explicit capability declarations, filters fail at runtime when connected incorrectly:
- Connecting float output to int32-expecting filter causes crashes
- Mismatched batch sizes cause buffer overflows
- Sample rate mismatches produce incorrect results

## Core Concepts

### Properties

The system tracks essential properties:
- **Data Type**: The sample data type (float, int32, etc.)
- **Batch Capacity**: Min/max batch sizes the filter can process
- **Sample Period**: Time between samples in nanoseconds (0 = variable/unknown)
- **Min/Max Throughput**: Guaranteed minimum and maximum samples per second
- **Max Total Samples**: Total number of samples a source will produce (0 = unlimited)
- **Channel Count**: Number of channels (future)

### Constraints and Behaviors

- **Input Constraints**: Requirements a filter has for its inputs
- **Output Behaviors**: How a filter generates or transforms properties
  - **SET**: Source filters use SET to define properties they generate
  - **PRESERVE**: Transform filters use PRESERVE to pass through properties

### Property Computation

Output properties are computed using `prop_propagate()` and cached in the filter:
1. For source filters: Propagating an UNKNOWN property table through SET behaviors
2. For transform filters: Propagating input properties through behaviors (PRESERVE/SET)

All property computation happens during the validation phase (not during connection). The validation traverses the DAG in topological order, computing and caching properties in `filter->output_properties[port]`.

### Unknown Properties

Properties remain UNKNOWN when:
- Source filters cannot determine them (e.g., file sources before reading)
- No behavior is specified for that property
- Upstream filters provide UNKNOWN and filter uses PRESERVE

UNKNOWN is a valid state that causes validation to fail if downstream filters require that property. This ensures explicit handling of missing information.

## Filter API Usage

### Declaring Input Requirements

Filters declare what they can accept during initialization:

```c
// CSV sink requires float data and known sample rate
prop_append_constraint(&sink->base, PROP_DATA_TYPE, CONSTRAINT_OP_EQ, 
                      &(SampleDtype_t){DTYPE_FLOAT});
prop_append_constraint(&sink->base, PROP_SAMPLE_PERIOD_NS, CONSTRAINT_OP_EXISTS, 
                      NULL);
```

### Declaring Output Behaviors

Filters declare how they generate or transform properties using behaviors:

```c
// Source filter: Uses SET behaviors to define output properties
prop_append_behavior(&sg->base, OUTPUT_0, PROP_DATA_TYPE,
                    BEHAVIOR_OP_SET, &(SampleDtype_t){DTYPE_FLOAT});
prop_append_behavior(&sg->base, OUTPUT_0, PROP_SAMPLE_PERIOD_NS,
                    BEHAVIOR_OP_SET, &sample_period_ns);

// Transform filter: Uses PRESERVE to pass through properties from input 0
prop_append_behavior(&filter->base, OUTPUT_0, PROP_DATA_TYPE,
                    BEHAVIOR_OP_PRESERVE, NULL);  // NULL operand defaults to input 0

// Multi-input router: Preserve from specific inputs
prop_append_behavior(&router->base, OUTPUT_0, PROP_DATA_TYPE,
                    BEHAVIOR_OP_PRESERVE, &(uint32_t){0});  // from input 0
prop_append_behavior(&router->base, OUTPUT_1, PROP_DATA_TYPE,
                    BEHAVIOR_OP_PRESERVE, &(uint32_t){1});  // from input 1
```

Note: Output properties are computed by propagating input properties (or UNKNOWN for sources) through these behaviors during validation.

### Buffer-Based Constraints

For filters that match their buffer configuration:

```c
// Map filter accepts batches matching its buffer config
// accepts_partial_fill = true means it can handle variable-sized batches
prop_constraints_from_buffer_append(&f->base, &config.buff_config, true);
```

### Declaring Output Behaviors

Filters declare how they transform or preserve properties:

```c
// Passthrough filter preserves all properties and allows partial batches
prop_set_output_behavior_for_buffer_filter(&pt->base, &config->buff_config,
                                          false,  // passthrough (not adapt)
                                          false); // allows partial batches

// Batch matcher adapts batch sizes and guarantees full batches
prop_set_output_behavior_for_buffer_filter(&matcher->base, &config.buff_config,
                                          true,   // adapt batch size
                                          true);  // guarantee full batches
```

## Connection Validation

The system performs validation at two levels:

### Local (Pairwise) Validation
**Current Implementation**: `filt_connect()` behavior:

1. **Simple connection**: `filt_connect()` just establishes DAG edges
2. **No validation**: Connection doesn't validate properties (they may not be computed yet)
3. **Order-independent**: Filters can be connected in any order
4. **Deferred validation**: Property validation happens during pipeline validation phase

**Future Enhancement**: The `prop_validate_connection()` function exists in the API but is not called during connection. It could be used for eager validation once property computation is implemented.

### Global (Pipeline) Validation
**Status**: IMPLEMENTED (see `pipeline_property_validation.md` for full specification)

Happens automatically during `pipeline_start()`:
1. **Linear Traversal**: Validates filters in array order (topological sort not yet implemented)
2. **Property Propagation**: Computes filter output properties using `prop_propagate()`
3. **End-to-End Validation**: Verifies complete data flow compatibility
4. **Multi-input Alignment**: Validates synchronized input requirements via `prop_validate_multi_input_alignment()`

## Handling Unknown Properties

### When Properties Are Unknown

Some source filters cannot determine all properties at initialization:

```c
// CSV source might not know sample rate until file is read
Bp_EC csv_source_init(CSVSource_t* src, CSVSource_config_t config)
{
    // Data type is known
    prop_append_behavior(&src->base, OUTPUT_0, PROP_DATA_TYPE,
                        BEHAVIOR_OP_SET, &(SampleDtype_t){DTYPE_FLOAT});
    
    // Sample rate might be unknown
    if (config.sample_rate_hz > 0) {
        // User provided it
        uint64_t period = 1000000000ULL / config.sample_rate_hz;
        prop_append_behavior(&src->base, OUTPUT_0, PROP_SAMPLE_PERIOD_NS,
                            BEHAVIOR_OP_SET, &period);
    }
    // No behavior = property remains UNKNOWN
}
```

### Validation with Unknown Properties

Validation correctly fails when required properties are UNKNOWN:

```
Pipeline: CSVSource → CSVSink
          ↓
  sample_period: UNKNOWN
                      ↓
              CSVSink requires: sample_period EXISTS
                      ↓
              VALIDATION FAILS
```

### Solutions for Unknown Properties

1. **Provide the property value at configuration**:
   ```c
   CSVSource_config_t config = {
       .sample_rate_hz = 48000  // User knows the rate
   };
   ```

2. **Add signal conditioning filters**:
   ```c
   // Pipeline: CSVSource → Resampler → CSVSink
   //                        ↑
   //                Sets sample_period
   ```

3. **Use tolerant downstream filters**:
   ```c
   // Use DebugOutput instead of CSVSink
   // DebugOutput doesn't require sample_period
   ```

## Examples

### CSV Sink Filter
```c
Bp_EC csv_sink_init(CSVSink_t* sink, CSVSink_config_t config)
{
    // ... initialization code ...
    
    // Require float32 data and known sample rate on input 0
    prop_append_constraint(&sink->base, INPUT_0, PROP_DATA_TYPE, 
                          CONSTRAINT_OP_EQ, &(SampleDtype_t){DTYPE_FLOAT});
    prop_append_constraint(&sink->base, INPUT_0, PROP_SAMPLE_PERIOD_NS, 
                          CONSTRAINT_OP_EXISTS, NULL);
    
    return Bp_EC_OK;
}
```

### Signal Generator (Source)
```c
Bp_EC signal_generator_init(SignalGenerator_t* sg, SignalGenerator_config_t config)
{
    // ... initialization code ...
    
    // Source filters use SET behaviors to define all output properties
    prop_append_behavior(&sg->base, OUTPUT_0, PROP_DATA_TYPE,
                        BEHAVIOR_OP_SET, &config.buff_config.dtype);
    
    prop_append_behavior(&sg->base, OUTPUT_0, PROP_SAMPLE_PERIOD_NS,
                        BEHAVIOR_OP_SET, &config.sample_period_ns);
    
    uint32_t batch_capacity = 1U << config.buff_config.batch_capacity_expo;
    prop_append_behavior(&sg->base, OUTPUT_0, PROP_MIN_BATCH_CAPACITY,
                        BEHAVIOR_OP_SET, &batch_capacity);
    prop_append_behavior(&sg->base, OUTPUT_0, PROP_MAX_BATCH_CAPACITY,
                        BEHAVIOR_OP_SET, &batch_capacity);
    
    // Set max total samples if specified (0 means unlimited)
    if (config.max_samples > 0) {
        prop_append_behavior(&sg->base, OUTPUT_0, PROP_MAX_TOTAL_SAMPLES,
                            BEHAVIOR_OP_SET, &config.max_samples);
    }
    
    return Bp_EC_OK;
}
```

### Map Filter (Transform)
```c
Bp_EC map_init(Map_filt_t* f, Map_config_t config)
{
    // ... initialization code ...
    
    // Accept batches matching buffer configuration on all inputs
    // true = accepts partial fills (variable batch sizes)
    prop_constraints_from_buffer_append(&f->base, INPUT_ALL, 
                                       &config.buff_config, true);
    
    // Preserve all properties on all outputs
    prop_append_behavior(&f->base, OUTPUT_ALL, PROP_DATA_TYPE, 
                        BEHAVIOR_OP_PRESERVE, NULL);
    prop_append_behavior(&f->base, OUTPUT_ALL, PROP_SAMPLE_PERIOD_NS, 
                        BEHAVIOR_OP_PRESERVE, NULL);
    
    return Bp_EC_OK;
}
```

### Multi-Input Alignment
**Status**: IMPLEMENTED - validation function exists and is called during pipeline validation.

For filters requiring aligned inputs, use `CONSTRAINT_OP_MULTI_INPUT_ALIGNED` on specific properties:

```c
// Element-wise operation - inputs 0 and 1 must have matching properties
prop_append_constraint(&filter->base, INPUT_0 | INPUT_1, PROP_DATA_TYPE,
                      CONSTRAINT_OP_MULTI_INPUT_ALIGNED, NULL);
prop_append_constraint(&filter->base, INPUT_0 | INPUT_1, PROP_SAMPLE_PERIOD_NS,
                      CONSTRAINT_OP_MULTI_INPUT_ALIGNED, NULL);

// Audio mixer - all inputs must have matching sample periods
prop_append_constraint(&mixer->base, INPUT_ALL, PROP_SAMPLE_PERIOD_NS,
                      CONSTRAINT_OP_MULTI_INPUT_ALIGNED, NULL);
```

When implemented, validation will verify that specified properties match across all indicated input ports.

### Multi-Port Filter Example
```c
// Stereo splitter: 1 stereo input → 2 mono outputs
Bp_EC stereo_splitter_init(StereoSplitter_t* splitter, config)
{
    // Input 0: Must be stereo float
    prop_append_constraint(&splitter->base, INPUT_0, PROP_DATA_TYPE,
                          CONSTRAINT_OP_EQ, &(SampleDtype_t){DTYPE_FLOAT});
    prop_append_constraint(&splitter->base, INPUT_0, PROP_CHANNEL_COUNT,
                          CONSTRAINT_OP_EQ, &(uint32_t){2});
    
    // Both outputs: Mono float, preserve sample rate
    prop_append_behavior(&splitter->base, OUTPUT_0 | OUTPUT_1, PROP_DATA_TYPE,
                        BEHAVIOR_OP_SET, &(SampleDtype_t){DTYPE_FLOAT});
    prop_append_behavior(&splitter->base, OUTPUT_0 | OUTPUT_1, PROP_CHANNEL_COUNT,
                        BEHAVIOR_OP_SET, &(uint32_t){1});
    prop_append_behavior(&splitter->base, OUTPUT_ALL, PROP_SAMPLE_PERIOD_NS,
                        BEHAVIOR_OP_PRESERVE, NULL);
}
```

### Throughput Constraints and Rate Limiting

Filters can declare minimum and maximum throughput requirements:

```c
// Throttle filter: Limits output rate to 48kHz max
Bp_EC throttle_init(ThrottleFilter_t* throttle, ThrottleConfig_t config)
{
    // ... initialization code ...
    
    // Declare max throughput limit
    uint32_t max_throughput = 48000;  // samples per second
    prop_append_behavior(&throttle->base, OUTPUT_0, PROP_MAX_THROUGHPUT_HZ,
                        BEHAVIOR_OP_SET, &max_throughput);
    
    // Preserve other properties
    prop_append_behavior(&throttle->base, OUTPUT_0, PROP_DATA_TYPE,
                        BEHAVIOR_OP_PRESERVE, NULL);
    
    return Bp_EC_OK;
}

// Real-time processing filter: Requires minimum throughput guarantee
Bp_EC realtime_processor_init(RealtimeProcessor_t* proc, ProcessorConfig_t config)
{
    // ... initialization code ...
    
    // Require minimum throughput from input
    uint32_t min_throughput = 44100;  // Need at least 44.1kHz
    prop_append_constraint(&proc->base, INPUT_0, PROP_MIN_THROUGHPUT_HZ,
                          CONSTRAINT_OP_GTE, &min_throughput);
    
    return Bp_EC_OK;
}
```

### Sample Limit Properties

Source filters can declare the total number of samples they will produce:

```c
// Test signal generator with finite output
SignalGenerator_config_t test_config = {
    .max_samples = 48000,  // Generate exactly 1 second at 48kHz
    .sample_period_ns = 20833,  // 48kHz
    // ...
};

// The signal generator will automatically set PROP_MAX_TOTAL_SAMPLES
// This allows downstream filters to know the stream is finite
```

## Migration Guide

### For New Filters

1. **Declare Input Constraints**: Use `prop_append_constraint()` in your init function
2. **Set Output Properties**: Use `prop_set_*()` functions for known output properties
3. **Use Helper Functions**: `prop_constraints_from_buffer_append()` for buffer-matching filters

### For Existing Filters

1. Remove any static contract definitions
2. Add constraint/property setup in the init function
3. Test that connections validate correctly

## API Reference

### Property Table Functions (for validation/testing)
- `prop_set_all_unknown(PropertyTable_t* table)` - Initialize table with all properties unknown
- `prop_get_dtype(PropertyTable_t* table, SampleDtype_t* dtype)` - Read property value
- `prop_get_sample_period(PropertyTable_t* table, uint64_t* period_ns)` - Read property value
- `prop_get_min_throughput(PropertyTable_t* table, uint32_t* throughput_hz)` - Read min throughput
- `prop_get_max_throughput(PropertyTable_t* table, uint32_t* throughput_hz)` - Read max throughput
- `prop_get_max_total_samples(PropertyTable_t* table, uint64_t* max_samples)` - Read sample limit

### Adding Constraints
- `prop_append_constraint(Filter_t* filter, uint32_t input_mask, SignalProperty_t prop, ConstraintOp_t op, const void* operand)` - Apply constraint to specific inputs via bitmask
- `prop_constraints_from_buffer_append(Filter_t* filter, uint32_t input_mask, const BatchBuffer_config* config, bool accepts_partial_fill)`

### Adding Output Behaviors
- `prop_append_behavior(Filter_t* filter, uint32_t output_mask, SignalProperty_t prop, BehaviorOp_t op, const void* operand)` - Apply behavior to specific outputs via bitmask
- `prop_set_output_behavior_for_buffer_filter(Filter_t* filter, uint32_t output_mask, const BatchBuffer_config* config, bool adapt_batch_size, bool guarantee_full)` - Helper for common filter patterns

### Port Targeting
Constraints and behaviors use bitmasks to specify which ports they apply to:
```c
#define INPUT_0     0x00000001
#define INPUT_1     0x00000002
#define INPUT_ALL   0xFFFFFFFF
#define OUTPUT_0    0x00000001
#define OUTPUT_ALL  0xFFFFFFFF

// Examples
prop_append_constraint(&filter->base, INPUT_ALL, PROP_DATA_TYPE, 
                      CONSTRAINT_OP_EQ, &dtype);  // All inputs
prop_append_constraint(&filter->base, INPUT_0 | INPUT_1, PROP_SAMPLE_PERIOD_NS,
                      CONSTRAINT_OP_MULTI_INPUT_ALIGNED, NULL);  // Inputs 0 and 1
```

### Validation Functions
- `prop_validate_connection(const PropertyTable_t* upstream, const FilterContract_t* downstream, char* error_msg, size_t size)` - Validate pairwise connection
- `prop_propagate(const PropertyTable_t* input_properties, size_t n_inputs, const FilterContract_t* filter, uint32_t output_port)` - Compute output properties by applying behaviors (see pipeline_property_validation.md for full spec)

### Constraint Operators
- `CONSTRAINT_OP_EXISTS`: Property must be known
- `CONSTRAINT_OP_EQ`: Property must equal specific value
- `CONSTRAINT_OP_GTE`: Property must be >= value
- `CONSTRAINT_OP_LTE`: Property must be <= value
- `CONSTRAINT_OP_MULTI_INPUT_ALIGNED`: Property must match across all inputs (proposed)

### Behavior Operators
- `BEHAVIOR_OP_SET`: Set property to specific value (operand contains the value)
- `BEHAVIOR_OP_PRESERVE`: Pass through property from specified input (operand.u32 contains input port, defaults to 0)

## Best Practices

1. **Be Explicit**: Always declare constraints for properties your filter depends on
2. **Set Output Properties**: Always set properties for data your filter produces
3. **Use Buffer Helpers**: Use `prop_constraints_from_buffer_append()` when appropriate
4. **Test Validation**: Verify that incompatible connections are properly rejected
5. **Handle Unknown Properties**: Source filters should leave properties UNKNOWN if they cannot be determined

## Pipeline Integration

### Pipeline as Filter
Pipelines inherit from `Filter_t` and must present a property contract like any filter. This contract is derived from the internal topology:

1. **Input Constraints**: Aggregated backward from all filters on the path from input to output
2. **Output Behaviors**: Composed forward along the path from input to output
3. **Encapsulation**: External code treats pipelines identically to atomic filters

See `docs/pipeline_property_validation.md` for detailed pipeline validation specification.

## Implementation Roadmap

### Phase 0-3: Core Infrastructure (COMPLETED)
- ✅ **Property propagation**: `prop_propagate()` computes output properties from behaviors
- ✅ **Pipeline validation**: `pipeline_validate_properties()` validates entire pipeline
- ✅ **Multi-input support**: prop_propagate accepts multiple input property tables
- ✅ **Multi-input alignment**: `prop_validate_multi_input_alignment()` validates aligned inputs
- ✅ **Error reporting**: Basic error messages with context
- ✅ **Source filter migration**: Signal generator and others use behaviors

### Phase 4: Advanced Features (NOT YET IMPLEMENTED)
- ❌ **Multi-output support**: Add MAX_OUTPUTS and output_properties array
- ❌ **Topological sort**: Full DAG traversal for complex pipelines
- ❌ **Nested pipelines**: Support external input property tables
- ❌ **Property negotiation**: Allow adaptive filters to query downstream requirements
- ❌ **Channel count**: Add channel property support

### Phase 5: Filter Migration (PARTIALLY COMPLETE)
**Completed**:
- Signal generator: Full constraint and behavior declarations
- CSV sink: Input constraints declared
- Map filter: Buffer-based constraints and behaviors
- Batch matcher: Adaptive behavior declarations

**Needs Migration**:
- CSV source: Add proper UNKNOWN handling for sample rate
- Remaining filters: Add constraint and behavior declarations
- Multi-output filters: Update to use port-specific properties

### Phase 4: Testing & Polish
- **Unit tests**: Property system operations
- **Integration tests**: End-to-end pipeline validation
- **Error cases**: Comprehensive mismatch scenarios
- **Documentation**: Complete examples and debugging guides

## Future Extensions

The system is designed to be extended with:
- Additional properties (channel count, latency, etc.)
- Complex constraint expressions
- Multi-input synchronization requirements
- Property negotiation and adaptation
- Constraint solver for automatic resolution of compatible settings

For implementation details and advanced features, see the appendix sections below.

---

## Appendix A: Advanced Constraint Examples

### Resampler Filter
A resampler might require specific input sample rates and produce different output rates:

```c
// Require 48kHz input (20833ns period)
uint64_t period_48khz = sample_rate_to_period_ns(48000);
prop_append_constraint(&filter->base, PROP_SAMPLE_PERIOD_NS, CONSTRAINT_OP_EQ, 
                      &period_48khz);

// Set 44.1kHz output (for single-output filter, use port 0)
prop_set_sample_period(&filter->base.output_properties[0], sample_rate_to_period_ns(44100));
```

### Multi-rate Filter
A filter supporting multiple sample rates:

```c
// Accept common audio rates
uint32_t rate = get_input_sample_rate();
if (rate == 44100 || rate == 48000 || rate == 96000) {
    prop_set_sample_period(&filter->base.output_properties[0], sample_rate_to_period_ns(rate));
} else {
    return Bp_EC_UNSUPPORTED_RATE;
}
```

## Appendix B: Property System Architecture

The property system uses embedded static arrays with explicit count tracking for efficient memory management. Each filter contains:
- Input constraint array (16 slots)
- Output behavior array (16 slots)
- Cached output properties table

Arrays use explicit count fields (n_input_constraints, n_output_behaviors) to track the number of active entries.

## Appendix C: Error Handling

**Current State**: Error codes are defined but validation is not yet implemented.

When implemented, validation will provide specific error codes:
- `Bp_EC_PROPERTY_MISMATCH`: Properties don't meet constraints
- `Bp_EC_INVALID_CONFIG`: Invalid constraint configuration
- `Bp_EC_TYPE_ERROR`: Data type mismatch

Error messages will be retrievable through the validation API (see `pipeline_property_validation.md` for examples).