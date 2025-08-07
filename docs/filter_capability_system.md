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

The system tracks four essential properties:
- **Data Type**: The sample data type (float, int32, etc.)
- **Batch Capacity**: Min/max batch sizes the filter can process
- **Sample Rate**: Fixed sample rate in Hz (0 = variable/unknown)

### Constraints and Behaviors

- **Input Constraints**: Requirements a filter has for its inputs
- **Output Behaviors**: How a filter sets or modifies properties

## Filter API Usage

### Declaring Input Requirements

Filters declare what they can accept during initialization:

```c
// CSV sink requires float data and known sample rate
prop_append_constraint(&sink->base, PROP_DATA_TYPE, CONSTRAINT_OP_EQ, 
                      &(SampleDtype_t){DTYPE_FLOAT});
prop_append_constraint(&sink->base, PROP_SAMPLE_RATE_HZ, CONSTRAINT_OP_EXISTS, 
                      NULL);
```

### Setting Output Properties

Source filters and transforms set their output properties:

```c
// Signal generator sets output properties
prop_set_dtype(&sg->base.output_properties, DTYPE_FLOAT);
prop_set_sample_rate(&sg->base.output_properties, 48000);
prop_set_min_batch_capacity(&sg->base.output_properties, batch_capacity);
prop_set_max_batch_capacity(&sg->base.output_properties, batch_capacity);
```

### Buffer-Based Constraints

For filters that match their buffer configuration:

```c
// Map filter accepts batches matching its buffer config
// accepts_partial_fill = true means it can handle variable-sized batches
prop_constraints_from_buffer_append(&f->base, &config.buff_config, true);
```

## Connection Validation

The system automatically validates connections when filters are connected:

1. **Automatic Validation**: `filt_connect()` checks property compatibility
2. **Error Messages**: Clear messages explain why connections fail
3. **Early Detection**: Problems caught at configuration time, not runtime

## Examples

### CSV Sink Filter
```c
Bp_EC csv_sink_init(CSVSink_t* sink, CSVSink_config_t config)
{
    // ... initialization code ...
    
    // Require float32 data and known sample rate
    prop_append_constraint(&sink->base, PROP_DATA_TYPE, CONSTRAINT_OP_EQ, 
                          &(SampleDtype_t){DTYPE_FLOAT});
    prop_append_constraint(&sink->base, PROP_SAMPLE_RATE_HZ, CONSTRAINT_OP_EXISTS, 
                          NULL);
    
    return Bp_EC_OK;
}
```

### Signal Generator (Source)
```c
Bp_EC signal_generator_init(SignalGenerator_t* sg, SignalGenerator_config_t config)
{
    // ... initialization code ...
    
    // Set output properties based on configuration
    prop_set_dtype(&sg->base.output_properties, config.buff_config.dtype);
    
    uint32_t sample_rate_hz = (uint32_t)(1000000000ULL / config.sample_period_ns);
    prop_set_sample_rate(&sg->base.output_properties, sample_rate_hz);
    
    uint32_t batch_capacity = 1U << config.buff_config.batch_capacity_expo;
    prop_set_min_batch_capacity(&sg->base.output_properties, batch_capacity);
    prop_set_max_batch_capacity(&sg->base.output_properties, batch_capacity);
    
    return Bp_EC_OK;
}
```

### Map Filter (Transform)
```c
Bp_EC map_init(Map_filt_t* f, Map_config_t config)
{
    // ... initialization code ...
    
    // Accept batches matching buffer configuration
    // true = accepts partial fills (variable batch sizes)
    prop_constraints_from_buffer_append(&f->base, &config.buff_config, true);
    
    // Map preserves all properties (no explicit output behaviors needed)
    
    return Bp_EC_OK;
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

### Setting Properties
- `prop_set_dtype(PropertyTable_t* table, SampleDtype_t dtype)`
- `prop_set_min_batch_capacity(PropertyTable_t* table, uint32_t capacity)`
- `prop_set_max_batch_capacity(PropertyTable_t* table, uint32_t capacity)`
- `prop_set_sample_rate(PropertyTable_t* table, uint32_t rate_hz)`

### Adding Constraints
- `prop_append_constraint(Filter_t* filter, SignalProperty_t prop, ConstraintOp_t op, const void* operand)`
- `prop_constraints_from_buffer_append(Filter_t* filter, const BatchBuffer_config* config, bool accepts_partial_fill)`

### Constraint Operators
- `CONSTRAINT_OP_EXISTS`: Property must be known
- `CONSTRAINT_OP_EQ`: Property must equal specific value
- `CONSTRAINT_OP_GTE`: Property must be >= value
- `CONSTRAINT_OP_LTE`: Property must be <= value

## Best Practices

1. **Be Explicit**: Always declare constraints for properties your filter depends on
2. **Set Output Properties**: Always set properties for data your filter produces
3. **Use Buffer Helpers**: Use `prop_constraints_from_buffer_append()` when appropriate
4. **Test Validation**: Verify that incompatible connections are properly rejected

## Future Extensions

The system is designed to be extended with:
- Additional properties (channel count, latency, etc.)
- Complex constraint expressions
- Multi-input synchronization requirements
- Property negotiation and adaptation

For implementation details and advanced features, see the appendix sections below.

---

## Appendix A: Advanced Constraint Examples

### Resampler Filter
A resampler might require specific input sample rates and produce different output rates:

```c
// Require 48kHz input
prop_append_constraint(&filter->base, PROP_SAMPLE_RATE_HZ, CONSTRAINT_OP_EQ, 
                      &(uint32_t){48000});

// Set 44.1kHz output
prop_set_sample_rate(&filter->base.output_properties, 44100);
```

### Multi-rate Filter
A filter supporting multiple sample rates:

```c
// Accept common audio rates
uint32_t rate = get_input_sample_rate();
if (rate == 44100 || rate == 48000 || rate == 96000) {
    prop_set_sample_rate(&filter->base.output_properties, rate);
} else {
    return Bp_EC_UNSUPPORTED_RATE;
}
```

## Appendix B: Property System Architecture

The property system uses embedded static arrays with sentinel values for efficient memory management. Each filter contains:
- Input constraint array (16 slots)
- Output behavior array (16 slots)
- Cached output properties table

Arrays use sentinel values (PROP_SENTINEL) to mark the end, eliminating the need for separate count variables.

## Appendix C: Error Handling

Connection validation provides specific error codes:
- `Bp_EC_PROPERTY_MISMATCH`: Properties don't meet constraints
- `Bp_EC_INVALID_CONFIG`: Invalid constraint configuration
- `Bp_EC_TYPE_ERROR`: Data type mismatch

Error messages can be retrieved for debugging (future enhancement).