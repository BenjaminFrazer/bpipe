# Property Validation User Guide

## Overview

The bpipe2 property validation system automatically validates data flow compatibility throughout your pipeline before execution, preventing runtime failures from mismatched data types, sample rates, or batch sizes.

## Quick Start

### Basic Pipeline with Validation

```c
// Create pipeline
Pipeline_t pipeline;
pipeline_init(&pipeline, config);

// Add filters
pipeline_add_filter(&pipeline, &signal_gen);
pipeline_add_filter(&pipeline, &passthrough);
pipeline_add_filter(&pipeline, &csv_sink);

// Connect filters
filt_connect(&signal_gen, 0, &passthrough, 0);
filt_connect(&passthrough, 0, &csv_sink, 0);

// Validation happens automatically at pipeline start
Bp_EC err = pipeline_start(&pipeline);
if (err != Bp_EC_OK) {
    // Pipeline validation failed - check error message
    fprintf(stderr, "Pipeline validation failed\n");
    return err;
}
```

## Key Concepts

### Properties

Properties describe the characteristics of data flowing through the pipeline:

- **Data Type** (`PROP_DATA_TYPE`): float, int32, etc.
- **Sample Period** (`PROP_SAMPLE_PERIOD_NS`): Time between samples in nanoseconds
- **Batch Capacity** (`PROP_MIN/MAX_BATCH_CAPACITY`): Min/max samples per batch
- **Channel Count** (`PROP_CHANNEL_COUNT`): Number of channels (future)

### Constraints

Filters declare what properties they require from their inputs:

```c
// CSV sink requires float data and known sample rate
prop_append_constraint(&sink->base, PROP_DATA_TYPE, 
                      CONSTRAINT_OP_EQ, &(SampleDtype_t){DTYPE_FLOAT}, INPUT_0);
prop_append_constraint(&sink->base, PROP_SAMPLE_PERIOD_NS, 
                      CONSTRAINT_OP_EXISTS, NULL, INPUT_0);
```

### Behaviors

Filters declare how they generate or transform properties:

```c
// Source filter sets output properties
prop_append_behavior(&source->base, PROP_DATA_TYPE,
                    BEHAVIOR_OP_SET, &dtype, OUTPUT_0);

// Transform filter preserves properties from input
prop_append_behavior(&filter->base, PROP_DATA_TYPE,
                    BEHAVIOR_OP_PRESERVE, NULL, OUTPUT_0);
```

## Common Patterns

### Source Filters

Source filters generate data and must define all output properties:

```c
Bp_EC my_source_init(MySource_t* src, MySource_config_t config)
{
    // ... initialization ...
    
    // Declare output properties using behaviors
    SampleDtype_t dtype = DTYPE_FLOAT;
    uint64_t period_ns = 1000000000 / config.sample_rate_hz;
    uint32_t batch_size = 1 << config.batch_expo;
    
    prop_append_behavior(&src->base, PROP_DATA_TYPE,
                        BEHAVIOR_OP_SET, &dtype, OUTPUT_0);
    prop_append_behavior(&src->base, PROP_SAMPLE_PERIOD_NS,
                        BEHAVIOR_OP_SET, &period_ns, OUTPUT_0);
    prop_append_behavior(&src->base, PROP_MIN_BATCH_CAPACITY,
                        BEHAVIOR_OP_SET, &batch_size, OUTPUT_0);
    
    // Compute output properties from behaviors
    PropertyTable_t unknown = prop_table_init();
    prop_set_all_unknown(&unknown);
    src->base.output_properties[0] = prop_propagate(NULL, 0, 
                                                     &src->base.contract, 0);
    
    return Bp_EC_OK;
}
```

### Transform Filters

Transform filters typically preserve properties while processing data:

```c
Bp_EC my_filter_init(MyFilter_t* f, MyFilter_config_t config)
{
    // ... initialization ...
    
    // Declare input constraints (what we accept)
    prop_constraints_from_buffer_append(&f->base, &config.buff_config, 
                                       true, INPUT_ALL);
    
    // Declare output behaviors (preserve all properties)
    prop_append_behavior(&f->base, PROP_DATA_TYPE,
                        BEHAVIOR_OP_PRESERVE, NULL, OUTPUT_ALL);
    prop_append_behavior(&f->base, PROP_SAMPLE_PERIOD_NS,
                        BEHAVIOR_OP_PRESERVE, NULL, OUTPUT_ALL);
    
    return Bp_EC_OK;
}
```

### Sink Filters

Sink filters only have input constraints:

```c
Bp_EC my_sink_init(MySink_t* sink, MySink_config_t config)
{
    // ... initialization ...
    
    // Declare what we require from input
    SampleDtype_t required_type = DTYPE_FLOAT;
    prop_append_constraint(&sink->base, PROP_DATA_TYPE,
                          CONSTRAINT_OP_EQ, &required_type, INPUT_0);
    prop_append_constraint(&sink->base, PROP_SAMPLE_PERIOD_NS,
                          CONSTRAINT_OP_EXISTS, NULL, INPUT_0);
    
    return Bp_EC_OK;
}
```

## Handling Unknown Properties

Some properties may be unknown at initialization time:

```c
// CSV source might not know sample rate
if (config.sample_rate_hz > 0) {
    // User provided sample rate
    uint64_t period = 1000000000ULL / config.sample_rate_hz;
    prop_append_behavior(&src->base, PROP_SAMPLE_PERIOD_NS,
                        BEHAVIOR_OP_SET, &period, OUTPUT_0);
}
// If not set, property remains UNKNOWN
```

If a downstream filter requires a property that is UNKNOWN, validation will fail with a clear error message.

## Multi-Input Filters

Filters with multiple inputs can require alignment:

```c
// Mixer requires all inputs to have same sample rate
prop_append_constraint(&mixer->base, PROP_SAMPLE_PERIOD_NS,
                      CONSTRAINT_OP_MULTI_INPUT_ALIGNED, NULL, INPUT_ALL);
```

## Pipeline Inputs

For filters that receive external data into the pipeline:

```c
// Declare which filter receives external input
pipeline_declare_external_input(&pipeline, 0, &input_filter, 0);

// Prepare external input properties for validation
PropertyTable_t external_inputs[1];
external_inputs[0] = prop_table_init();
prop_set_dtype(&external_inputs[0], DTYPE_FLOAT);
prop_set_sample_period(&external_inputs[0], period_ns);

// Validate with external inputs
pipeline_validate_properties(&pipeline, external_inputs, 1, error_msg, sizeof(error_msg));
```

## Error Messages

Validation provides detailed error messages:

```
Property validation failed at 'CSVSink':
  Input 0 property 'sample_period': UNKNOWN
  Constraint: sample_period EXISTS
  CSVSink requires sample_period but upstream provides UNKNOWN
```

## Troubleshooting

### Common Issues

1. **"Property X is UNKNOWN"**
   - Source filter doesn't set this property
   - Add the property behavior to the source filter
   - Or use a filter that sets this property

2. **"Property mismatch"**
   - Upstream and downstream have incompatible properties
   - Add a converter filter between them
   - Or reconfigure one of the filters

3. **"Multi-input alignment failed"**
   - Multiple inputs have different properties
   - Ensure all inputs have matching properties
   - Or use a resampler/converter before the multi-input filter

### Debugging Tips

1. **Check filter initialization**: Ensure all filters properly declare constraints and behaviors
2. **Trace property flow**: Properties propagate from sources through transforms to sinks
3. **Use debug output**: Add logging to see computed properties at each stage

## Performance Considerations

- Validation happens once at pipeline start, not during data processing
- Property tables are cached after computation
- Topological sort ensures efficient single-pass validation
- No runtime overhead after successful validation

## Migration Guide

For existing filters not using the property system:

1. **Add input constraints**: What properties does your filter require?
2. **Add output behaviors**: How does your filter transform properties?
3. **Test validation**: Ensure your filter validates correctly in pipelines
4. **Update documentation**: Document your filter's property requirements

## API Reference

See `bpipe/properties.h` for the complete API:

- `prop_append_constraint()` - Add input requirement
- `prop_append_behavior()` - Add output behavior
- `prop_propagate()` - Compute output properties
- `prop_set_all_unknown()` - Initialize unknown properties
- `prop_constraints_from_buffer_append()` - Helper for buffer-based filters

## Best Practices

1. **Be explicit**: Always declare all constraints and behaviors
2. **Handle UNKNOWN**: Don't assume properties will be known
3. **Use helpers**: Leverage helper functions for common patterns
4. **Test thoroughly**: Validate your filters in various pipeline configurations
5. **Document requirements**: Make your filter's requirements clear to users