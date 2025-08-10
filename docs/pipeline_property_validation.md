# Pipeline Property Validation Specification

## Overview

This document specifies how property validation works across an entire filter pipeline (DAG), including propagation of properties through filters and validation of multi-input alignment constraints.

## Problem Statement

While pairwise connection validation catches obvious mismatches, it cannot:
1. Determine actual output properties when filters modify them (e.g., batch size adaptation)
2. Validate multi-input alignment requirements across converging paths
3. Detect issues that only appear after property propagation through intermediate filters

## Solution: Topological Property Propagation

### Core Principle

Top-level pipelines are self-contained with no external inputs - all sources are internal and must fully define their output properties through SET behaviors. Nested pipelines receive external input properties from their containing pipeline during validation.

### Algorithm Overview

1. **Initialize Starting Points**: 
   - For source filters: Propagate UNKNOWN property table through SET behaviors
   - For external inputs: Use provided property tables from container
2. **Topological Traversal**: Process filters in dependency order
3. **Property Propagation**: Compute output properties by applying behaviors to input properties
4. **Constraint Validation**: At each filter, validate:
   - Input properties meet filter constraints
   - Multi-input alignment requirements are satisfied


### Core Validation Function

```c
Bp_EC pipeline_validate_properties(const Pipeline_t* pipeline,
                                   PropertyTable_t* external_inputs,
                                   size_t n_external_inputs,
                                   char* error_msg, 
                                   size_t error_msg_size);
```

For top-level pipelines, `external_inputs` is NULL and `n_external_inputs` is 0.
For nested pipelines, external inputs are provided by the containing pipeline.

## Property Propagation Rules

### 1. Source Filters
Source filters (no inputs) propagate from UNKNOWN through their SET behaviors:
```c
PropertyTable_t unknown;
prop_set_all_unknown(&unknown);
computed_props[source_idx] = propagate_properties(source, &unknown, 0);
```

Source filters use SET behaviors for properties they can determine:
```c
// Signal generator - knows all properties
prop_append_behavior(&sg->base, OUTPUT_0, PROP_DATA_TYPE, 
                    BEHAVIOR_OP_SET, &(SampleDtype_t){DTYPE_FLOAT});
prop_append_behavior(&sg->base, OUTPUT_0, PROP_SAMPLE_PERIOD_NS,
                    BEHAVIOR_OP_SET, &period_ns);

// CSV source - might not know sample rate
prop_append_behavior(&csv->base, OUTPUT_0, PROP_DATA_TYPE,
                    BEHAVIOR_OP_SET, &(SampleDtype_t){DTYPE_FLOAT});
// No behavior for SAMPLE_PERIOD_NS if unknown - stays UNKNOWN
```

Properties without SET behaviors remain UNKNOWN, which is a valid state that will cause validation to fail if downstream filters require them.

### 2. Transform Filters
Transform filters compute output properties based on behaviors:

#### PRESERVE Behavior
```c
// Input: float, 48kHz, batch=1-256
// Behavior: PRESERVE all
// Output: float, 48kHz, batch=1-256
output_prop = input_prop;
```

#### SET Behavior
```c
// Input: float, 48kHz, batch=1-256
// Behavior: SET batch_min=256, SET batch_max=256
// Output: float, 48kHz, batch=256-256
output_prop = input_prop;
output_prop.batch_min = 256;
output_prop.batch_max = 256;
```

#### Adaptive Behavior
```c
// Batch matcher adapts to downstream requirements
// Input: float, 48kHz, batch=1-256
// Detected sink needs: batch=512
// Output: float, 48kHz, batch=512-512
```

### 3. Multi-Input Filters
When multiple inputs converge:

1. **Validate Alignment**: Check MULTI_INPUT_ALIGNED constraints
2. **Merge Properties**: Determine output based on all inputs
3. **Apply Behaviors**: Transform merged properties per filter rules

## Multi-Input Validation

### Alignment Constraints

```c
// Element-wise operation requires full alignment
prop_append_constraint(&filter->base, PROP_DATA_TYPE,
                      CONSTRAINT_OP_MULTI_INPUT_ALIGNED, NULL);
prop_append_constraint(&filter->base, PROP_SAMPLE_PERIOD_NS,
                      CONSTRAINT_OP_MULTI_INPUT_ALIGNED, NULL);
```

### Validation Process

```c
// For each multi-input filter
for each input_property in filter.inputs {
    // Check regular constraints
    validate_constraints(input_property, filter.constraints);
    
    // Check alignment with other inputs
    if (has_alignment_constraint(filter, property)) {
        for each other_input {
            if (input_property[prop] != other_input[prop]) {
                return ERROR("Property %s not aligned across inputs", prop);
            }
        }
    }
}
```

## Example Validation Flow

### Pipeline Structure
```
SignalGen1 → Passthrough → Mixer → BatchMatcher → Sink
SignalGen2 ────────────────↗
```

### Validation Steps

1. **Process SignalGen1** (source)
   - Output: float, 48kHz, batch=256

2. **Process SignalGen2** (source)
   - Output: float, 48kHz, batch=128

3. **Process Passthrough**
   - Input: float, 48kHz, batch=256
   - Behaviors: PRESERVE all
   - Output: float, 48kHz, batch=256

4. **Process Mixer** (multi-input)
   - Input1: float, 48kHz, batch=256
   - Input2: float, 48kHz, batch=128
   - Constraints: MULTI_INPUT_ALIGNED on sample_period
   - Validation: ✓ Both 48kHz (aligned)
   - Output: float, 48kHz, batch=1-256 (can handle variable)

5. **Process BatchMatcher**
   - Input: float, 48kHz, batch=1-256
   - Behaviors: ADAPT batch size, GUARANTEE full
   - Output: float, 48kHz, batch=512-512 (detected from sink)

6. **Process Sink**
   - Input: float, 48kHz, batch=512
   - Constraints: dtype=float, sample_period EXISTS
   - Validation: ✓ All constraints met

## Validation Strategy

### Connection-Time (Local)
- Basic pairwise compatibility check during `filt_connect()`
- Catches obvious errors early (type mismatches, etc.)
- Limited to directly connected filters

### Pipeline-Wide (Global)
- Full DAG traversal before pipeline start
- Propagates properties through entire graph
- Validates with computed properties, not just declared ones
- Detects issues requiring global context

## Error Reporting

Error messages should include:
1. Which filters are involved
2. Which property failed validation
3. Expected vs actual values (including UNKNOWN)
4. Path through pipeline to failure point

### Common Validation Failures

#### Unknown Property Required
```
Property validation failed at 'CSVSink':
  Input 0 (from CSVSource): sample_period = UNKNOWN
  Constraint: sample_period EXISTS
  CSVSink requires sample_period but upstream provides UNKNOWN
  
  Solutions:
  - Provide sample_rate_hz when configuring CSVSource
  - Add a resampler or other filter that sets sample_period
  - Use a sink that doesn't require sample_period
```

#### Property Mismatch
```
Property validation failed at 'Mixer':
  Input 0 (from Passthrough): sample_period = 20833ns (48kHz)
  Input 1 (from SignalGen2): sample_period = 22675ns (44.1kHz)
  Constraint: MULTI_INPUT_ALIGNED on sample_period
  All inputs must have matching sample periods
```

#### Type Mismatch
```
Property validation failed at 'IntProcessor':
  Input 0 (from FloatSource): data_type = FLOAT
  Constraint: data_type == INT32
  IntProcessor requires INT32 but upstream provides FLOAT
```

## Usage Pattern

```c
// Build pipeline
Pipeline_t pipeline;
pipeline_init(&pipeline, config);

// Make connections (basic validation happens here)
filt_connect(source, 0, sink, 0);  // Quick local checks

// Validate entire pipeline before starting
Bp_EC err = pipeline_validate_properties(&pipeline, NULL, 0, 
                                        error_msg, sizeof(error_msg));
if (err != Bp_EC_OK) {
    printf("Validation failed: %s\n", error_msg);
    return err;
}

// Start pipeline (properties are now guaranteed valid)
pipeline_start(&pipeline);
```

## API Functions

### Main Validation
```c
Bp_EC pipeline_validate_properties(const Pipeline_t* pipeline,
                                   PropertyTable_t* external_inputs,
                                   size_t n_external_inputs,
                                   char* error_msg, size_t error_msg_size);
```

- Top-level pipelines: Call with `external_inputs=NULL, n_external_inputs=0`
- Nested pipelines: Provide actual input properties from containing pipeline


## Testing Strategy

### Unit Tests
1. Linear pipeline (source → transform → sink)
2. Branching pipeline (one source → multiple sinks)
3. Merging pipeline (multiple sources → one sink)
4. Complex DAG with multiple merge/branch points

### Property Tests
1. Property preservation through passthrough
2. Property modification through adapters
3. Multi-input alignment validation
4. Constraint violation detection

### Error Case Tests
1. Mismatched data types
2. Incompatible sample rates
3. Unaligned multi-inputs
4. Cyclic dependencies

## Nested Pipeline Handling

### Overview

Pipelines can contain other pipelines as filters, creating a hierarchical structure. The validation system must handle this recursively while maintaining efficiency and clear error reporting.

### Pipeline as Filter - Property Encapsulation

A `Pipeline_t` contains a `Filter_t base` member, making it appear as a regular filter to containing pipelines. The key principle is **complete encapsulation**: the pipeline presents an explicitly declared external contract, hiding its internal topology.

#### Property Contract Declaration

Pipelines must explicitly declare their external contract during initialization, just like any other filter:

1. **Input Constraints**: What the pipeline requires from external inputs
2. **Output Behaviors**: How the pipeline transforms or generates properties

The pipeline designer is responsible for ensuring the declared contract matches what the internal topology actually does. This is verified through compliance testing.

#### Example Contract Declaration

```c
Bp_EC audio_pipeline_init(Pipeline_t* pipe, Pipeline_config_t config)
{
    // ... internal filter setup ...
    
    // Explicitly declare what this pipeline requires
    prop_append_constraint(&pipe->base, INPUT_0, PROP_DATA_TYPE,
                          CONSTRAINT_OP_EQ, &(SampleDtype_t){DTYPE_FLOAT});
    prop_append_constraint(&pipe->base, INPUT_0, PROP_SAMPLE_PERIOD_NS,
                          CONSTRAINT_OP_EXISTS, NULL);
    
    // Explicitly declare what this pipeline outputs
    prop_append_behavior(&pipe->base, OUTPUT_0, PROP_DATA_TYPE,
                        BEHAVIOR_OP_PRESERVE, NULL);
    prop_append_behavior(&pipe->base, OUTPUT_0, PROP_SAMPLE_PERIOD_NS,
                        BEHAVIOR_OP_PRESERVE, NULL);
    
    return Bp_EC_OK;
}
```

This encapsulation means:
- External code never needs to know internal structure
- Pipelines compose naturally as black-box filters
- Validation works identically for atomic filters and pipelines

### Validation Approach

#### Validation Process

1. **Initialize starting points**: Sources propagate UNKNOWN through SET behaviors
2. **Apply external inputs**: For nested pipelines, use provided property tables
3. **Topological traversal**: Process filters in dependency order
4. **Property propagation**: Apply behaviors to compute outputs
5. **Constraint validation**: Check all requirements are met

#### Pipeline Internal Validation

During validation, pipelines:

1. **Validate internal topology**: Use provided external inputs (or none for top-level)
2. **Check contract accuracy**: Verify declared behaviors match actual behavior
3. **Report context**: Include nested pipeline path in error messages

The pipeline's declared contract is trusted by outer pipelines - internal validation happens separately to verify correctness.

### Example: Nested Pipeline Validation

```
Outer Pipeline:
  SignalGen → [NestedPipeline] → Sink
  
  NestedPipeline:
    Input → Passthrough → BatchMatcher → Output
```

Validation sequence:
1. Validate NestedPipeline internally:
   - Input has no constraints
   - Passthrough preserves properties
   - BatchMatcher adapts batch size
   - Output properties computed: float, 48kHz, batch=512
2. Set NestedPipeline's external properties:
   - Input constraints: none (from Input filter)
   - Output properties: float, 48kHz, batch=512 (from Output filter)
3. Validate Outer Pipeline:
   - SignalGen output: float, 48kHz, batch=256
   - NestedPipeline accepts any input (no constraints)
   - NestedPipeline output: float, 48kHz, batch=512
   - Sink validates against: float, 48kHz, batch=512 ✓

### Error Reporting for Nested Pipelines

Include full path in error messages:

```
Property validation failed:
  In pipeline 'Main':
    In nested pipeline 'AudioProcessor':
      In nested pipeline 'EffectsChain':
        At filter 'Reverb':
          Input sample_period (22675ns) doesn't match required (20833ns)
  Path: Main → AudioProcessor → EffectsChain → Reverb
```

### Performance Considerations

1. **Cache Validation Results**: Don't re-validate unchanged nested pipelines
2. **Lazy Validation**: Only validate when pipeline structure changes
3. **Partial Validation**: Validate only affected sub-graphs when possible

### Special Cases

#### Recursive Pipeline References
Detect and prevent infinite recursion:
```c
if (is_in_ancestor_chain(pipeline, nested)) {
    return Bp_EC_CIRCULAR_REFERENCE;
}
```

#### Dynamic Pipeline Modification
If nested pipelines can change at runtime:
- Invalidate cached validation results
- Re-validate affected portions
- Propagate changes to parent pipelines

## Future Enhancements

1. **Incremental Validation**: Re-validate only affected portions when pipeline changes
2. **Property Negotiation**: Allow filters to adjust properties based on downstream needs
3. **Constraint Solving**: Automatically find valid property configurations
4. **Runtime Property Updates**: Support dynamic property changes during execution
5. **Pipeline Templates**: Pre-validated pipeline patterns for common use cases