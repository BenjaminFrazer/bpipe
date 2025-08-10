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

## Future Enhancements

1. **Incremental Validation**: Re-validate only affected portions when pipeline changes
2. **Property Negotiation**: Allow filters to adjust properties based on downstream needs
3. **Constraint Solving**: Automatically find valid property configurations
4. **Runtime Property Updates**: Support dynamic property changes during execution