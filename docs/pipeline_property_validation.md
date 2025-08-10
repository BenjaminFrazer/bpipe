# Pipeline Property Validation Specification

## Implementation Status

**This document describes the pipeline-wide property validation system.**

**Current Status**: IMPLEMENTED (Phases 0-3)
- ✅ Pipeline-wide validation via `pipeline_validate_properties()` 
- ✅ Property propagation through `prop_propagate()` with multi-input support
- ✅ Validation automatically called during `pipeline_start()`
- ✅ Source filters using behaviors and prop_propagate
- ✅ Multi-input alignment validation
- ✅ Input property tables for filters (input_properties[MAX_INPUTS])
- ❌ Multi-output support (single output_properties only)
- ❌ Nested pipeline external inputs (top-level pipelines only)
- See `filter_capability_system.md` for detailed feature status

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
computed_props[source_idx] = prop_propagate(&unknown, 0, &source->contract, 0);
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

## Property Propagation Function Specification

### Function Signature
```c
PropertyTable_t prop_propagate(
    const PropertyTable_t* input_properties,  /* Array of input property tables */
    size_t n_inputs,                          /* Number of input tables */
    const FilterContract_t* filter_contract,  /* Filter's behavior definitions */
    uint32_t output_port                      /* Which output port to compute properties for */
);
/* Returns: PropertyTable for the specified output port */
```

### When Called
- **Source filters**: During validation phase (inputs are UNKNOWN)
- **Transform filters**: During validation phase (uses actual input properties)
- **Multi-output filters**: Called once per output port (0 to n_outputs-1)
- **Validation phase only**: All property computation happens during pipeline validation

### Behavior Rules

1. **Default Preservation**: If no behavior is specified for a property, it is automatically preserved from input 0 (or remains UNKNOWN if no inputs).

2. **Behavior Application**: For each property:
   - If a behavior with matching `output_mask` applies to this `output_port`:
     - **SET**: Property takes the specified value, regardless of input
     - **PRESERVE**: Property is copied from the input port specified in `operand.u32` (defaults to 0 if not specified)
   - If no behavior matches: Property is preserved from input 0

3. **Multi-Input Handling**:
   - PRESERVE uses the input port specified in `operand.u32`
   - If multiple inputs need the same value, use CONSTRAINT_OP_MULTI_INPUT_ALIGNED to ensure they match, then preserve from any
   - Multi-input alignment is validated separately (not part of propagation)

4. **UNKNOWN Propagation**:
   - UNKNOWN is propagated unless a SET behavior provides a value
   - PRESERVE of UNKNOWN remains UNKNOWN
   - No inputs (source filters) start with all properties UNKNOWN

5. **Error Conditions**:
   - Multiple behaviors targeting the same property for the same output port is an error
   - Returns table with all properties UNKNOWN on error

### Usage Examples

```c
// Source filter: Apply behaviors to UNKNOWN inputs
PropertyTable_t unknown = prop_table_init();
prop_set_all_unknown(&unknown);
filter->output_properties = prop_propagate(&unknown, 0, &filter->contract, 0);

// Transform filter: Apply behaviors to actual inputs
filter->output_properties = prop_propagate(filter->input_properties, 
                                          filter->n_inputs, 
                                          &filter->contract, 0);

// Multi-input filter with selective preservation
// Output 0 preserves from input 0, Output 1 preserves from input 1
OutputBehavior_t behaviors[] = {
    {PROP_DATA_TYPE, BEHAVIOR_OP_PRESERVE, OUTPUT_0, {.u32 = 0}},  // from input 0
    {PROP_DATA_TYPE, BEHAVIOR_OP_PRESERVE, OUTPUT_1, {.u32 = 1}},  // from input 1
    {PROP_SAMPLE_PERIOD_NS, BEHAVIOR_OP_PRESERVE, OUTPUT_ALL, {.u32 = 0}} // all from input 0
};

// Multi-output filter: Compute properties for each output port
for (int port = 0; port < filter->n_outputs; port++) {
    filter->output_properties[port] = prop_propagate(filter->input_properties,
                                                     filter->n_inputs,
                                                     &filter->contract, port);
}
```

### Implementation Notes

- Function is pure: no side effects, deterministic output
- Called once per output port during validation phase only
- The `output_port` parameter filters which behaviors apply based on output_mask
- Not called during connection (connection just builds DAG)

### Caching Strategy

Properties are computed once and cached in `filter->output_properties[port]` because:
1. **Performance**: Avoids recomputation if validation is run multiple times
2. **Consistency**: Single source of truth for filter's output properties
3. **Static nature**: Behaviors don't change after filter initialization

**When computed and cached**:
- **During validation only**: All filters compute properties during pipeline validation
- **Topological order**: Sources first, then filters in dependency order
- **Recomputation**: If pipeline structure changes, validation must be re-run

### Structural Requirements

For multi-output support, the Filter structure needs:
```c
#define MAX_OUTPUTS 8  // Maximum number of output ports per filter

typedef struct _Filter_t {
    // ... other fields ...
    PropertyTable_t output_properties[MAX_OUTPUTS];  // One table per output port
    uint32_t n_outputs;                              // Actual number of outputs
    // ... other fields ...
} Filter_t;
```

Single-output filters can use `output_properties[0]` for backward compatibility.
Most filters have 1 output, but multi-output filters (routers, splitters) need multiple property tables.


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

### During Pipeline Initialization
```c
// Define all connections upfront
Connection_t connections[] = {
    {&signalGen1, 0, &passthrough, 0},
    {&passthrough, 0, &mixer, 0},
    {&signalGen2, 0, &mixer, 1},
    {&mixer, 0, &batchMatcher, 0},
    {&batchMatcher, 0, &sink, 0}
};

// Pipeline init creates all connections
Pipeline_config_t config = {
    .filters = all_filters,
    .n_filters = 5,
    .connections = connections,
    .n_connections = 5,
    // ...
};
pipeline_init(&pipeline, config);  // Connections made here
```

### During Validation Phase (Topological Order)

The validation traverses the DAG and computes properties:

1. **Process SignalGen1** (source)
   - Compute: `prop_propagate(UNKNOWN, ...)` with SET behaviors
   - Result: float, 48kHz, batch=256

2. **Process SignalGen2** (source) 
   - Compute: `prop_propagate(UNKNOWN, ...)` with SET behaviors
   - Result: float, 48kHz, batch=128

3. **Process Passthrough** (has all inputs ready)
   - Input properties: [SignalGen1: float, 48kHz, batch=256]
   - Compute: `prop_propagate(inputs, ...)` with PRESERVE behaviors
   - Result: float, 48kHz, batch=256

4. **Process Mixer** (has all inputs ready)
   - Input properties: [Passthrough: float, 48kHz, batch=256], [SignalGen2: float, 48kHz, batch=128]
   - Validate: MULTI_INPUT_ALIGNED on sample_period ✓ (both 48kHz)
   - Compute: `prop_propagate(inputs, ...)` 
   - Result: float, 48kHz, batch=1-256 (variable)

5. **Process BatchMatcher** (has all inputs ready)
   - Input properties: [Mixer: float, 48kHz, batch=1-256]
   - Compute: `prop_propagate(inputs, ...)` with ADAPT behaviors
   - Result: float, 48kHz, batch=512-512

6. **Validate Sink** (terminal node)
   - Input properties: [BatchMatcher: float, 48kHz, batch=512]
   - Validate constraints: dtype=float ✓, sample_period EXISTS ✓
   - No output computation needed (sink has no outputs)

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

## Pipeline Construction and Validation Sequence

### Phase 1: Filter Initialization
```c
// 1. Create and initialize filters
SignalGenerator_t source;
signal_generator_init(&source, config);  // Sets behaviors, computes properties for sources

Map_filt_t transform; 
map_init(&transform, config);  // Sets behaviors, but can't compute properties yet (no inputs)

CSVSink_t sink;
csv_sink_init(&sink, config);  // Sets constraints
```

**At this point**:
- Source filters have computed `output_properties` (from UNKNOWN inputs)
- Transform/sink filters have behaviors/constraints but no computed properties

### Phase 2: Connection (Order-Independent)
```c
// 2. Connect filters in any order - just establishes the DAG structure
filt_connect(&source, 0, &transform, 0);  
filt_connect(&transform, 0, &sink, 0);

// Or connect in reverse order - doesn't matter!
filt_connect(&transform, 0, &sink, 0);
filt_connect(&source, 0, &transform, 0);
```

**During connection**:
- Simply records the connection in the DAG structure
- No validation performed (properties may not be computed yet)
- No property propagation (deferred to validation phase)

### Handling Complex Topologies

For DAGs with multiple paths and convergence points:

```c
// Example: Diamond topology
//     Source1 → Transform1 ↘
//                           Mixer → Sink
//     Source2 → Transform2 ↗

// Connect in ANY order - just building the graph
filt_connect(&mixer, 0, &sink, 0);           // Can start anywhere
filt_connect(&transform1, 0, &mixer, 0);     
filt_connect(&source2, 0, &transform2, 0);   
filt_connect(&transform2, 0, &mixer, 1);     
filt_connect(&source1, 0, &transform1, 0);   // Order doesn't matter
```

**Connection is simple**:
- Just establishes edges in the DAG
- No computation or validation
- Order-independent

### Phase 3: Pipeline Validation (Required Before Execution)
```c
// 3. Validate entire pipeline DAG
Pipeline_t pipeline;
pipeline_init(&pipeline, config);
pipeline_add_filter(&pipeline, &source);
pipeline_add_filter(&pipeline, &transform);
pipeline_add_filter(&pipeline, &sink);

Bp_EC err = pipeline_validate_properties(&pipeline, NULL, 0, 
                                        error_msg, sizeof(error_msg));
if (err != Bp_EC_OK) {
    printf("Validation failed: %s\n", error_msg);
    return err;
}
```

**During validation** (happens in topological order):
1. **Identify sources** (filters with no inputs)
2. **Compute source properties**: `prop_propagate(UNKNOWN, ...)`
3. **Traverse DAG topologically**:
   - For each filter in dependency order:
     - Validate input properties against constraints
     - Compute output properties via `prop_propagate()`
     - Store computed properties for downstream filters
4. **Check multi-input alignment** where required
5. **Report any validation errors** with full context

### Phase 4: Pipeline Execution
```c
// 4. Start pipeline (properties are now guaranteed valid)
pipeline_start(&pipeline);
```

## Usage Pattern

```c
// Build pipeline
Pipeline_t pipeline;
pipeline_init(&pipeline, config);

// Make connections (no validation, just DAG building)
filt_connect(source, 0, sink, 0);  // Just establishes edges

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

#### Pipeline Validation Function Behavior

When `pipeline_validate_properties()` is called:

1. **Initialize sources or use external inputs**: Top-level uses internal sources, nested uses provided inputs
2. **Validate internal topology**: Propagate properties through internal filters
3. **Verify contract**: Ensure declared behaviors match computed outputs
4. **Report errors with context**: Include full path for nested pipelines
