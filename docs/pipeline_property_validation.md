# Pipeline Property Validation Specification

## Overview

This document specifies how property validation works across an entire filter pipeline (DAG), including propagation of properties through filters and validation of multi-input alignment constraints.

## Problem Statement

While pairwise connection validation catches obvious mismatches, it cannot:
1. Determine actual output properties when filters modify them (e.g., batch size adaptation)
2. Validate multi-input alignment requirements across converging paths
3. Detect issues that only appear after property propagation through intermediate filters

## Solution: Topological Property Propagation

### Algorithm Overview

1. **Build Dependency Graph**: Identify filter connections and dependencies
2. **Find Sources**: Locate filters with no inputs (sources) or external inputs
3. **Topological Traversal**: Process filters in dependency order
4. **Property Propagation**: Compute each filter's actual output properties based on:
   - Input properties (computed from upstream)
   - Filter's declared behaviors (PRESERVE, SET, etc.)
5. **Constraint Validation**: At each filter, validate:
   - Input properties meet filter constraints
   - Multi-input alignment requirements are satisfied

### Data Structures

```c
typedef struct {
    PropertyTable_t computed_props[MAX_FILTERS];  // Computed output per filter
    bool visited[MAX_FILTERS];                    // Traversal tracking
    size_t in_degree[MAX_FILTERS];               // Number of inputs per filter
} ValidationState_t;
```

### Core Validation Function

```c
Bp_EC pipeline_validate_properties(const Pipeline_t* pipeline,
                                   char* error_msg, 
                                   size_t error_msg_size);
```

## Property Propagation Rules

### 1. Source Filters
Source filters use their declared `output_properties` directly:
```c
computed_props[source_idx] = source->output_properties;
```

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

## Implementation Phases

### Phase 1: Core Algorithm
- Topological sort implementation
- Basic property propagation
- Single-input validation

### Phase 2: Multi-Input Support
- Alignment constraint validation
- Property merging for multi-input filters
- Comprehensive error messages

### Phase 3: Advanced Features
- Cycle detection in DAG
- Partial pipeline validation
- Property conflict resolution

## Error Reporting

Error messages should include:
1. Which filters are involved
2. Which property failed validation
3. Expected vs actual values
4. Path through pipeline to failure point

Example:
```
Property validation failed at 'Mixer':
  Input 0 (from Passthrough): sample_period = 20833ns (48kHz)
  Input 1 (from SignalGen2): sample_period = 22675ns (44.1kHz)
  Constraint: MULTI_INPUT_ALIGNED on sample_period
  All inputs must have matching sample periods
```

## API Functions

### Main Validation
```c
Bp_EC pipeline_validate_properties(const Pipeline_t* pipeline,
                                   char* error_msg, size_t error_msg_size);
```

### Helper Functions
```c
// Build validation state from pipeline
ValidationState_t* build_validation_state(const Pipeline_t* pipeline);

// Find source filters in pipeline
void find_source_filters(const Pipeline_t* pipeline, 
                        Filter_t** sources, size_t* n_sources);

// Propagate properties through single filter
PropertyTable_t propagate_through_filter(const PropertyTable_t* inputs[],
                                         size_t n_inputs,
                                         const FilterContract_t* contract);

// Validate multi-input alignment
Bp_EC validate_multi_input_alignment(const PropertyTable_t* inputs[],
                                     size_t n_inputs,
                                     const InputConstraint_t* constraints,
                                     size_t n_constraints);
```

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

### Pipeline as Filter

A `Pipeline_t` contains a `Filter_t base` member, making it appear as a regular filter to containing pipelines. The pipeline's external interface is determined by its designated input and output filters.

### Validation Approach

#### 1. Recursive Validation (Depth-First)

```c
Bp_EC pipeline_validate_properties(const Pipeline_t* pipeline,
                                   char* error_msg, size_t error_msg_size)
{
    // Step 1: Validate nested pipelines first
    for (size_t i = 0; i < pipeline->n_filters; i++) {
        if (pipeline->filters[i]->filt_type == FILT_T_PIPELINE) {
            Pipeline_t* nested = (Pipeline_t*)pipeline->filters[i];
            
            // Recursively validate nested pipeline
            Bp_EC err = pipeline_validate_properties(nested, error_msg, error_msg_size);
            if (err != Bp_EC_OK) {
                // Prepend context to error message
                prepend_context(error_msg, "In nested pipeline '%s': ", nested->base.name);
                return err;
            }
        }
    }
    
    // Step 2: Validate current level
    return validate_pipeline_level(pipeline, error_msg, error_msg_size);
}
```

#### 2. Property Computation for Nested Pipelines

After validating a nested pipeline's internals, compute its external properties:

```c
void compute_nested_pipeline_properties(Pipeline_t* nested,
                                        ValidationState_t* parent_state)
{
    // The nested pipeline's output properties are the computed properties
    // of its designated output filter after internal validation
    size_t output_filter_idx = find_filter_index(nested, nested->output_filter);
    PropertyTable_t* output_props = &nested->internal_computed_props[output_filter_idx];
    
    // Set the pipeline filter's output properties for parent validation
    nested->base.output_properties = *output_props;
    
    // The pipeline's input constraints come from its input filter
    if (nested->input_filter) {
        copy_constraints(&nested->base, nested->input_filter);
    }
}
```

#### 3. Connection Mapping

When validating connections to/from nested pipelines:

```c
// Connection to nested pipeline input
if (to_filter->filt_type == FILT_T_PIPELINE) {
    Pipeline_t* nested = (Pipeline_t*)to_filter;
    // Validate against the nested pipeline's input filter constraints
    Filter_t* actual_input = nested->input_filter;
    validate_connection(from_filter, actual_input);
}

// Connection from nested pipeline output  
if (from_filter->filt_type == FILT_T_PIPELINE) {
    Pipeline_t* nested = (Pipeline_t*)from_filter;
    // Use computed properties from nested pipeline's output filter
    PropertyTable_t* actual_output_props = get_computed_props(nested->output_filter);
    validate_with_properties(actual_output_props, to_filter);
}
```

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