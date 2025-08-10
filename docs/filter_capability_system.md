# Filter Capability and Requirements System

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
- **Channel Count**: Number of channels (future)

### Constraints and Behaviors

- **Input Constraints**: Requirements a filter has for its inputs
- **Output Behaviors**: How a filter generates or transforms properties
  - **SET**: Source filters use SET to define properties they generate
  - **PRESERVE**: Transform filters use PRESERVE to pass through properties

### Property Computation

Output properties are not stored directly but computed during validation by:
1. For source filters: Propagating an UNKNOWN property table through SET behaviors
2. For transform filters: Propagating input properties through behaviors (PRESERVE/SET)

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

// Transform filter: Uses PRESERVE to pass through properties
prop_append_behavior(&filter->base, OUTPUT_0, PROP_DATA_TYPE,
                    BEHAVIOR_OP_PRESERVE, NULL);
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
Currently implemented - happens automatically during `filt_connect()`:

1. **Automatic Validation**: `filt_connect()` checks property compatibility between two filters
2. **Error Messages**: Clear messages explain why connections fail (though currently lost)
3. **Early Detection**: Obvious problems caught at connection time
4. **Limitation**: Only validates direct connections, not the entire pipeline

### Global (Pipeline) Validation
Proposed but not yet implemented - would happen before pipeline start:

1. **Graph Traversal**: Walk entire filter DAG to propagate properties
2. **Property Inference**: Compute intermediate filter properties from behaviors
3. **End-to-End Validation**: Verify complete data flow compatibility
4. **See**: `specs/pipeline_property_validation.md` for design details

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

### Multi-Input Alignment (Proposed)
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
- `prop_propagate(const PropertyTable_t* upstream, const FilterContract_t* filter)` - Propagate properties through a filter (not yet wired up)

### Constraint Operators
- `CONSTRAINT_OP_EXISTS`: Property must be known
- `CONSTRAINT_OP_EQ`: Property must equal specific value
- `CONSTRAINT_OP_GTE`: Property must be >= value
- `CONSTRAINT_OP_LTE`: Property must be <= value
- `CONSTRAINT_OP_MULTI_INPUT_ALIGNED`: Property must match across all inputs (proposed)

### Behavior Operators
- `BEHAVIOR_OP_SET`: Set property to specific value
- `BEHAVIOR_OP_PRESERVE`: Pass through upstream property unchanged

## Best Practices

1. **Be Explicit**: Always declare constraints for properties your filter depends on
2. **Set Output Properties**: Always set properties for data your filter produces
3. **Use Buffer Helpers**: Use `prop_constraints_from_buffer_append()` when appropriate
4. **Test Validation**: Verify that incompatible connections are properly rejected

## Pipeline Integration

### Pipeline as Filter
Pipelines inherit from `Filter_t` and must present a property contract like any filter. This contract is derived from the internal topology:

1. **Input Constraints**: Aggregated backward from all filters on the path from input to output
2. **Output Behaviors**: Composed forward along the path from input to output
3. **Encapsulation**: External code treats pipelines identically to atomic filters

See `docs/pipeline_property_validation.md` for detailed pipeline validation specification.

## Remaining Work

### Core System Improvements
- **Pipeline-wide validation**: Implement graph traversal to propagate properties through entire pipeline
- **Property propagation**: Actually apply declared behaviors (PRESERVE, SET) to compute filter output properties
- **Error message retrieval**: Currently error messages are lost - need API to retrieve them
- **Multi-input handling**: Support property merging for filters with multiple inputs
- **Pipeline contract computation**: Implement backward constraint aggregation and forward behavior composition

### Missing Filter Features
- **Output behavior declarations**: Many filters declare constraints but not how they transform properties
- **Dynamic property updates**: Support for filters that determine properties at start time
- **Property negotiation**: Allow filters to adapt based on downstream requirements
- **Channel count property**: Add support for multi-channel data streams

### Testing & Validation
- **Comprehensive connection tests**: Test all constraint operators (GTE, LTE, EXISTS, EQ)
- **Pipeline validation tests**: Test end-to-end property flow through complex DAGs
- **Error case coverage**: Test all property mismatch scenarios
- **Multi-input synchronization**: Test property conflicts from multiple sources

### Documentation & Examples
- **Complete migration guide**: Document all filters that still need migration
- **Property flow diagrams**: Visual representation of property propagation
- **Best practices guide**: When to use constraints vs behaviors
- **Debugging guide**: How to troubleshoot property validation failures

### Critical Gaps
- **Intermediate filter properties never computed**: Filters declare behaviors but don't apply them
- **No way to validate before start**: Need explicit validation API
- **Silent failures**: Validation errors provide no feedback to users
- **Incomplete filter migration**: Not all filters use the property system yet

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

// Set 44.1kHz output
prop_set_sample_period(&filter->base.output_properties, sample_rate_to_period_ns(44100));
```

### Multi-rate Filter
A filter supporting multiple sample rates:

```c
// Accept common audio rates
uint32_t rate = get_input_sample_rate();
if (rate == 44100 || rate == 48000 || rate == 96000) {
    prop_set_sample_period(&filter->base.output_properties, sample_rate_to_period_ns(rate));
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

Connection validation provides specific error codes:
- `Bp_EC_PROPERTY_MISMATCH`: Properties don't meet constraints
- `Bp_EC_INVALID_CONFIG`: Invalid constraint configuration
- `Bp_EC_TYPE_ERROR`: Data type mismatch

Error messages can be retrieved for debugging (future enhancement).