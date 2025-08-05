# Filter Capability and Requirements System

## Overview

This document describes a minimal property system for filters to explicitly declare their critical requirements, starting with the most problematic compatibility issues: data type mismatches, batch capacity mismatches, and sample rate incompatibilities.

## Problem Statement

Currently, filters make implicit assumptions about:
- Data types (connecting float to int32 filter crashes)
- Batch capacity requirements (64-sample filter to 1024-sample buffer fails)
- Sample rate expectations (48kHz source to 44.1kHz-expecting filter)

These implicit assumptions lead to:
- Runtime failures when incompatible filters are connected
- Difficulty debugging pipeline configuration issues
- No automated way to validate basic compatibility

## Minimal Viable Solution (Phase 1)

### Core Properties

Starting with just 4 critical properties that address 90% of connection failures:

```c
typedef enum {
    // Data type property
    PROP_DATA_TYPE,              // SampleDtype_t value
    
    // Batch capacity properties (in samples, not exponents)
    PROP_MIN_BATCH_CAPACITY,     // Minimum accepted batch size in samples
    PROP_MAX_BATCH_CAPACITY,     // Maximum accepted batch size in samples
    
    // Timing property
    PROP_SAMPLE_RATE_HZ,         // Fixed sample rate in Hz (0 = variable/unknown)
    
    PROP_COUNT_MVP  // Just 4 properties for MVP
} SignalProperty_t;
```

### Simplified Operators

Only the essential operators needed for basic validation:

```c
typedef enum {
    CONSTRAINT_OP_EXISTS,    // Property must be present
    CONSTRAINT_OP_EQ,        // Property must equal value
    CONSTRAINT_OP_GTE,       // Property must be >= value (for capacities)
    CONSTRAINT_OP_LTE,       // Property must be <= value (for capacities)
} ConstraintOp_t;

typedef enum {
    BEHAVIOR_OP_SET,         // Set property to fixed value
    BEHAVIOR_OP_PRESERVE,    // Pass through unchanged (default)
} BehaviorOp_t;
```

### Minimal Implementation

```c
// Simple property storage
typedef struct {
    bool known;
    union {
        SampleDtype_t dtype;
        uint32_t u32;
    } value;
} Property_t;

typedef struct {
    Property_t properties[PROP_COUNT_MVP];
} PropertyTable_t;

// Minimal constraint structure
typedef struct {
    SignalProperty_t property;
    ConstraintOp_t op;
    union {
        SampleDtype_t dtype;
        uint32_t u32;
    } operand;
} InputConstraint_t;

// Minimal behavior structure
typedef struct {
    SignalProperty_t property;
    BehaviorOp_t op;
    union {
        SampleDtype_t dtype;
        uint32_t u32;
    } operand;
} OutputBehavior_t;

// Filter contract
typedef struct {
    InputConstraint_t* input_constraints;
    size_t n_input_constraints;
    OutputBehavior_t* output_behaviors;
    size_t n_output_behaviors;
} FilterContract_t;
```

## Example Contracts

### CSV Sink (Strict Type Requirements)

```c
// CSV sink requires float32 data
static const InputConstraint_t csv_sink_constraints[] = {
    { PROP_DATA_TYPE, CONSTRAINT_OP_EQ, {.dtype = DTYPE_FLOAT} },
    { PROP_SAMPLE_RATE_HZ, CONSTRAINT_OP_EXISTS, {0} },  // Must know sample rate
};

static const FilterContract_t csv_sink_contract = {
    .input_constraints = csv_sink_constraints,
    .n_input_constraints = 2,
    .output_behaviors = NULL,  // Sink has no outputs
    .n_output_behaviors = 0,
};
```

### Batch Buffer Filter (Capacity Constraints)

```c
// Batch buffer can accept 64-1024 samples, outputs same
static const InputConstraint_t batch_buffer_constraints[] = {
    { PROP_MIN_BATCH_CAPACITY, CONSTRAINT_OP_GTE, {.u32 = 64} },
    { PROP_MAX_BATCH_CAPACITY, CONSTRAINT_OP_LTE, {.u32 = 1024} },
};

// Output behaviors match input constraints
static const OutputBehavior_t batch_buffer_behaviors[] = {
    { PROP_MIN_BATCH_CAPACITY, BEHAVIOR_OP_SET, {.u32 = 64} },
    { PROP_MAX_BATCH_CAPACITY, BEHAVIOR_OP_SET, {.u32 = 1024} },
};
```

### Signal Generator (Sets Properties)

```c
// Signal generator has no inputs
static const OutputBehavior_t signal_gen_behaviors[] = {
    { PROP_DATA_TYPE, BEHAVIOR_OP_SET, {.dtype = DTYPE_FLOAT} },
    { PROP_MIN_BATCH_CAPACITY, BEHAVIOR_OP_SET, {.u32 = 1} },     // Can produce any size
    { PROP_MAX_BATCH_CAPACITY, BEHAVIOR_OP_SET, {.u32 = 65536} }, // Up to 64k samples
    { PROP_SAMPLE_RATE_HZ, BEHAVIOR_OP_SET, {.u32 = 48000} },
};

static const FilterContract_t signal_gen_contract = {
    .input_constraints = NULL,
    .n_input_constraints = 0,
    .output_behaviors = signal_gen_behaviors,
    .n_output_behaviors = 4,
};
```

### Simple Resampler (48kHz → 44.1kHz)

```c
// Requires exactly 48kHz input
static const InputConstraint_t resampler_48to44_constraints[] = {
    { PROP_SAMPLE_RATE_HZ, CONSTRAINT_OP_EQ, {.u32 = 48000} },
};

// Outputs 44.1kHz
static const OutputBehavior_t resampler_48to44_behaviors[] = {
    { PROP_SAMPLE_RATE_HZ, BEHAVIOR_OP_SET, {.u32 = 44100} },
};
```

## Validation Examples

### Type Mismatch Detection

```c
// Source outputs float
PropertyTable_t source_props = {
    .properties = {
        [PROP_DATA_TYPE] = { .known = true, .value.dtype = DTYPE_FLOAT }
    }
};

// Sink requires int32
InputConstraint_t sink_wants[] = {
    { PROP_DATA_TYPE, CONSTRAINT_OP_EQ, {.dtype = DTYPE_I32} }
};

// Connection fails - type mismatch
bool valid = validate_connection(&source_props, &sink_contract);  // Returns false
```

### Batch Capacity Validation

```c
// Source can produce up to 64 samples
PropertyTable_t source_props = {
    .properties = {
        [PROP_MAX_BATCH_CAPACITY] = { .known = true, .value.u32 = 64 }
    }
};

// Sink requires at least 128 samples
InputConstraint_t sink_wants[] = {
    { PROP_MIN_BATCH_CAPACITY, CONSTRAINT_OP_GTE, {.u32 = 128} }
};

// Connection fails - source can't meet minimum requirement
bool valid = validate_connection(&source_props, &sink_contract);  // Returns false
```

## Benefits of Minimal Approach

1. **Immediate Impact**: Catches most common configuration errors
2. **Simple Implementation**: ~200 lines of code for core functionality
3. **Easy to Test**: Clear validation rules
4. **Low Risk**: Small change to existing system
5. **Learning Platform**: Validate design before expanding

## Future Expansion (Phase 2+)

Once the minimal system proves valuable, consider adding:
- Channel count properties
- Timestamp properties (present, monotonic, etc.)
- Signal bounds (min/max values)
- More complex operators (SCALE, RANGE, etc.)
- Multi-input constraint groups

## Full Proposed Solution (Future)

### 1. Operator-Based Property System

The system uses operators to express both input requirements and output transformations, providing a flexible and compositional approach to property management.

#### Input Constraints

Input constraints are expressed as operators that validate incoming properties:

```c
typedef enum {
    // Comparison operators
    CONSTRAINT_OP_EQ,        // Property must equal value
    CONSTRAINT_OP_NEQ,       // Property must not equal value
    CONSTRAINT_OP_GT,        // Property must be greater than value
    CONSTRAINT_OP_GTE,       // Property must be greater than or equal
    CONSTRAINT_OP_LT,        // Property must be less than value
    CONSTRAINT_OP_LTE,       // Property must be less than or equal
    CONSTRAINT_OP_RANGE,     // Property must be within range [min, max]
    
    // Existence operators
    CONSTRAINT_OP_EXISTS,    // Property must be present
    CONSTRAINT_OP_NOT_EXISTS,// Property must not be present
    
    // Type operators
    CONSTRAINT_OP_TYPE_EQ,   // Data type must match
    CONSTRAINT_OP_TYPE_IN,   // Data type must be in set
    
    // Custom validation
    CONSTRAINT_OP_CUSTOM,    // Custom validation function
} ConstraintOp_t;

typedef struct {
    SignalProperty_t property;
    ConstraintOp_t op;
    union {
        float f32;
        uint32_t u32;
        struct { float min, max; } range;
        SampleDtype_t dtype;
        SampleDtype_t* dtype_set;
        bool (*validator)(const PropertyValue_t* value, void* context);
    } operand;
} InputConstraint_t;
```

#### Output Behaviors

Output behaviors are expressed as operators that transform properties:

```c
typedef enum {
    // Value operations
    BEHAVIOR_OP_SET,         // Set property to fixed value
    BEHAVIOR_OP_SCALE,       // Multiply property by factor
    BEHAVIOR_OP_DIVIDE,      // Divide property by divisor
    BEHAVIOR_OP_ADD,         // Add offset to property
    BEHAVIOR_OP_CLAMP,       // Clamp property to range
    
    // State operations
    BEHAVIOR_OP_DESTROY,     // Remove property
    BEHAVIOR_OP_PRESERVE,    // Pass through unchanged (default)
    BEHAVIOR_OP_ADAPT,       // Adapt based on input (e.g., match input type)
    
    // Complex operations
    BEHAVIOR_OP_CUSTOM,      // Custom mutation function
    BEHAVIOR_OP_DERIVE,      // Derive from other properties
} BehaviorOp_t;

typedef struct {
    SignalProperty_t property;
    BehaviorOp_t op;
    union {
        float f32;
        uint32_t u32;
        struct { float min, max; } range;
        PropertyValue_t (*mutator)(const PropertyValue_t* input, void* context);
        struct {
            SignalProperty_t source_prop;
            PropertyValue_t (*derive)(const PropertyValue_t* source);
        } derive;
    } operand;
} OutputBehavior_t;
```

#### Property Enumeration

Properties are categorized by their domain:

```c
typedef enum {
    // Timing properties
    PROP_TIMESTAMP_PRESENT,
    PROP_TIMESTAMP_MONOTONIC,
    PROP_TIMESTAMP_CONTINUOUS,
    PROP_TIMESTAMP_ABSOLUTE,
    PROP_SAMPLE_RATE_FIXED,
    PROP_PHASE_LOCKED,
    PROP_MAX_JITTER_KNOWN,
    PROP_MAX_LATENCY_KNOWN,
    
    // Data properties
    PROP_DATA_VALUES_EXACT,
    PROP_DATA_ORDERING_PRESERVED,
    PROP_DATA_COMPLETE,
    PROP_DATA_UNIQUE,
    
    // Signal bounds (split into individual properties)
    PROP_SIGNAL_MIN_BOUNDED,      // Has guaranteed minimum
    PROP_SIGNAL_MAX_BOUNDED,      // Has guaranteed maximum
    PROP_SIGNAL_CONTINUOUS,
    PROP_SIGNAL_CAUSAL,
    PROP_SIGNAL_MONOTONIC_INC,    // Monotonically increasing
    PROP_SIGNAL_MONOTONIC_DEC,    // Monotonically decreasing
    
    // Mathematical properties
    PROP_LINEAR_SYSTEM,
    PROP_TIME_INVARIANT,
    PROP_PHASE_SHIFT_KNOWN,       // Has known phase shift
    PROP_GAIN_KNOWN,              // Has known gain factor
    PROP_DC_OFFSET_KNOWN,         // Has known DC offset
    
    // Quality metrics (individual properties)
    PROP_MIN_SNR_KNOWN,           // Has guaranteed minimum SNR
    PROP_MAX_THD_KNOWN,           // Has guaranteed maximum THD
    PROP_MAX_NOISE_FLOOR_KNOWN,   // Has guaranteed noise floor
    
    // Statistical properties
    PROP_MEAN_KNOWN,              // Has known mean value
    PROP_VARIANCE_BOUNDED,        // Has maximum variance
    PROP_GAUSSIAN_DISTRIBUTION,
    PROP_STATIONARY,
    
    // Domain properties
    PROP_UNITS_KNOWN,
    PROP_CALIBRATED,
    PROP_METADATA_PRESERVED,
    PROP_ERROR_BOUNDS_KNOWN,
    
    PROP_COUNT  // Total number of properties
} SignalProperty_t;
```

### 2. Filter Contract Structure

```c
typedef struct {
    // Input constraints - array terminated by property == PROP_COUNT
    InputConstraint_t* input_constraints;
    size_t n_input_constraints;
    
    // Output behaviors - array terminated by property == PROP_COUNT
    OutputBehavior_t* output_behaviors;
    size_t n_output_behaviors;
    
    // Optional: constraint groups for multi-input filters
    struct {
        uint32_t input_mask;  // Which inputs this group applies to
        InputConstraint_t* constraints;
        size_t n_constraints;
    } *input_groups;
} FilterContract_t;
```

### 3. Property Value Storage

Properties are stored in a table indexed by the property enumeration, with each property having a known flag, type, and value:

```c
typedef enum {
    PROP_TYPE_U32,
    PROP_TYPE_I32,
    PROP_TYPE_F32,
    PROP_TYPE_BOOL,
    PROP_TYPE_ENUM,
} PropertyType_t;

typedef enum {
    PROP_CLASS_NUMERIC,    // Can be scaled, added, etc.
    PROP_CLASS_BOOLEAN,    // Can only be set/destroyed
    PROP_CLASS_ENUM,       // Can only be set to specific values
    PROP_CLASS_SPECIAL,    // Custom handling required
} PropertyClass_t;

// Individual property entry
typedef struct {
    bool known;
    PropertyType_t type;
    union {
        uint32_t u32;
        int32_t i32;
        float f32;
        bool b;
        int enum_val;
    } value;
} Property_t;

// Property table - array indexed by SignalProperty_t
typedef struct {
    Property_t properties[PROP_COUNT];
} PropertyTable_t;

// Property metadata - defines canonical type for each property
static const struct {
    PropertyClass_t class;
    PropertyType_t type;
    const char* name;
} property_info[PROP_COUNT] = {
    [PROP_SAMPLE_RATE_FIXED]    = { PROP_CLASS_NUMERIC, PROP_TYPE_U32,  "sample_rate_hz" },
    [PROP_MAX_JITTER_KNOWN]     = { PROP_CLASS_NUMERIC, PROP_TYPE_U32,  "max_jitter_ns" },
    [PROP_MAX_LATENCY_KNOWN]    = { PROP_CLASS_NUMERIC, PROP_TYPE_U32,  "max_latency_ns" },
    
    [PROP_SIGNAL_MIN_BOUNDED]   = { PROP_CLASS_NUMERIC, PROP_TYPE_F32,  "signal_min" },
    [PROP_SIGNAL_MAX_BOUNDED]   = { PROP_CLASS_NUMERIC, PROP_TYPE_F32,  "signal_max" },
    [PROP_PHASE_SHIFT_KNOWN]    = { PROP_CLASS_NUMERIC, PROP_TYPE_F32,  "phase_shift_rad" },
    [PROP_GAIN_KNOWN]           = { PROP_CLASS_NUMERIC, PROP_TYPE_F32,  "gain_linear" },
    [PROP_DC_OFFSET_KNOWN]      = { PROP_CLASS_NUMERIC, PROP_TYPE_F32,  "dc_offset" },
    
    [PROP_TIMESTAMP_PRESENT]    = { PROP_CLASS_BOOLEAN, PROP_TYPE_BOOL, "has_timestamp" },
    [PROP_TIMESTAMP_MONOTONIC]  = { PROP_CLASS_BOOLEAN, PROP_TYPE_BOOL, "timestamp_monotonic" },
    [PROP_DATA_VALUES_EXACT]    = { PROP_CLASS_BOOLEAN, PROP_TYPE_BOOL, "values_exact" },
    
    [PROP_CHANNEL_ORDER]        = { PROP_CLASS_ENUM,    PROP_TYPE_ENUM, "channel_order" },
    // ... other properties
};
```

### 4. Validation and Propagation Algorithms

```c
// Initialize property table with correct types
PropertyTable_t init_property_table(void) {
    PropertyTable_t table = {0};
    for (int p = 0; p < PROP_COUNT; p++) {
        table.properties[p].known = false;
        table.properties[p].type = property_info[p].type;
    }
    return table;
}

// Validate connection between filters
bool validate_connection(const PropertyTable_t* upstream_props,
                        const FilterContract_t* downstream_contract) {
    // Check each input constraint
    for (size_t i = 0; i < downstream_contract->n_input_constraints; i++) {
        InputConstraint_t* c = &downstream_contract->input_constraints[i];
        Property_t* prop = &upstream_props->properties[c->property];
        
        switch (c->op) {
            case CONSTRAINT_OP_EXISTS:
                if (!prop->known) return false;
                break;
                
            case CONSTRAINT_OP_GT:
                if (!prop->known) return false;
                // Type-safe comparison
                switch (prop->type) {
                    case PROP_TYPE_U32:
                        if (prop->value.u32 <= c->operand.u32) return false;
                        break;
                    case PROP_TYPE_F32:
                        if (prop->value.f32 <= c->operand.f32) return false;
                        break;
                }
                break;
                
            case CONSTRAINT_OP_RANGE:
                if (!prop->known) return false;
                if (prop->type == PROP_TYPE_F32) {
                    if (prop->value.f32 < c->operand.range.min || 
                        prop->value.f32 > c->operand.range.max) return false;
                }
                break;
                
            case CONSTRAINT_OP_CUSTOM:
                if (!c->operand.validator(prop, downstream_contract))
                    return false;
                break;
        }
    }
    return true;
}

// Propagate properties through a filter (inheritance + behaviors)
PropertyTable_t propagate_properties(const PropertyTable_t* upstream,
                                   const FilterContract_t* filter) {
    // Start with upstream properties (inheritance by default)
    PropertyTable_t downstream = *upstream;
    
    // Apply filter's output behaviors
    for (size_t i = 0; i < filter->n_output_behaviors; i++) {
        OutputBehavior_t* b = &filter->output_behaviors[i];
        Property_t* prop = &downstream.properties[b->property];
        
        switch (b->op) {
            case BEHAVIOR_OP_PRESERVE:
                // Already inherited from upstream - do nothing
                break;
                
            case BEHAVIOR_OP_SET:
                prop->known = true;
                prop->value = b->operand;
                break;
                
            case BEHAVIOR_OP_DESTROY:
                prop->known = false;
                break;
                
            case BEHAVIOR_OP_SCALE:
                if (prop->known && property_info[b->property].class == PROP_CLASS_NUMERIC) {
                    // Type-safe scaling based on property's type
                    switch (prop->type) {
                        case PROP_TYPE_F32:
                            prop->value.f32 *= b->operand.f32;
                            break;
                        case PROP_TYPE_U32:
                            prop->value.u32 = (uint32_t)(prop->value.u32 * b->operand.f32);
                            break;
                        case PROP_TYPE_I32:
                            prop->value.i32 = (int32_t)(prop->value.i32 * b->operand.f32);
                            break;
                    }
                }
                break;
                
            case BEHAVIOR_OP_ADD:
                if (prop->known && property_info[b->property].class == PROP_CLASS_NUMERIC) {
                    switch (prop->type) {
                        case PROP_TYPE_F32:
                            prop->value.f32 += b->operand.f32;
                            break;
                        case PROP_TYPE_U32:
                            prop->value.u32 += b->operand.u32;
                            break;
                    }
                }
                break;
                
            case BEHAVIOR_OP_CUSTOM:
                if (prop->known) {
                    prop->value = b->operand.mutator(&prop->value, NULL);
                }
                break;
        }
    }
    
    return downstream;
}
```

### 5. Property Access Patterns

With the array-based design, property access is clean and consistent:

```c
// Setting a property
PropertyTable_t props = init_property_table();
props.properties[PROP_SAMPLE_RATE_FIXED].known = true;
props.properties[PROP_SAMPLE_RATE_FIXED].value.u32 = 48000;

// Checking and reading a property
if (props.properties[PROP_SAMPLE_RATE_FIXED].known) {
    uint32_t rate = props.properties[PROP_SAMPLE_RATE_FIXED].value.u32;
}

// Iterating over all properties
for (int p = 0; p < PROP_COUNT; p++) {
    if (props.properties[p].known) {
        printf("Property %s is set\n", property_info[p].name);
    }
}

// Example pipeline propagation
PropertyTable_t pipeline_properties(void) {
    PropertyTable_t props = init_property_table();
    
    // SignalGen: Sets initial properties
    props = propagate_properties(&props, &signal_gen_contract);
    // Now has: sample_rate=48000, signal_min=-1.0, signal_max=1.0
    
    // Gain(2.0): Scales signal bounds, inherits sample rate
    props = propagate_properties(&props, &gain_contract);
    // Now has: sample_rate=48000 (inherited), signal_min=-2.0, signal_max=2.0
    
    // Resampler: Changes sample rate, inherits bounds
    props = propagate_properties(&props, &resampler_contract);
    // Now has: sample_rate=44100 (set), signal_min=-2.0 (inherited), signal_max=2.0 (inherited)
    
    return props;
}
```

## Example Filter Contracts

### Resampler Filter (48kHz → 44.1kHz)

```c
// Resampler that converts 48kHz → 44.1kHz
static const InputConstraint_t resampler_constraints[] = {
    // Must have fixed sample rate
    { PROP_SAMPLE_RATE_FIXED, CONSTRAINT_OP_EXISTS, {0} },
    { PROP_SAMPLE_RATE_FIXED, CONSTRAINT_OP_EQ, {.u32 = 48000} },
    
    // Must have bounded jitter for quality resampling
    { PROP_MAX_JITTER_KNOWN, CONSTRAINT_OP_EXISTS, {0} },
    { PROP_MAX_JITTER_KNOWN, CONSTRAINT_OP_LT, {.u32 = 1000} }, // < 1μs
};

static const OutputBehavior_t resampler_behaviors[] = {
    // Set new sample rate
    { PROP_SAMPLE_RATE_FIXED, BEHAVIOR_OP_SET, {.u32 = 44100} },
    
    // Scale jitter by rate ratio (44100/48000 ≈ 0.919)
    { PROP_MAX_JITTER_KNOWN, BEHAVIOR_OP_SCALE, {.f32 = 0.919f} },
    
    // Add resampling latency
    { PROP_MAX_LATENCY_KNOWN, BEHAVIOR_OP_ADD, {.u32 = 50} }, // 50 samples
    
    // Destroy exact value property due to interpolation
    { PROP_DATA_VALUES_EXACT, BEHAVIOR_OP_DESTROY, {0} },
};

static const FilterContract_t resampler_contract = {
    .input_constraints = resampler_constraints,
    .n_input_constraints = ARRAY_SIZE(resampler_constraints),
    .output_behaviors = resampler_behaviors,
    .n_output_behaviors = ARRAY_SIZE(resampler_behaviors),
};
```

### Gain Filter

```c
// For a float32 gain filter with gain=2.0
static const OutputBehavior_t gain_behaviors[] = {
    // Numeric properties - operand interpreted as float32
    { PROP_SIGNAL_MIN_BOUNDED, BEHAVIOR_OP_SCALE, {.f32 = 2.0f} },
    { PROP_SIGNAL_MAX_BOUNDED, BEHAVIOR_OP_SCALE, {.f32 = 2.0f} },
    { PROP_VARIANCE_BOUNDED, BEHAVIOR_OP_SCALE, {.f32 = 4.0f} },  // gain²
    
    // Scale SNR inversely (noise also amplified)
    { PROP_MIN_SNR_KNOWN, BEHAVIOR_OP_ADD, {.f32 = -6.02f} }, // -20*log10(2)
    
    // Update gain property
    { PROP_GAIN_KNOWN, BEHAVIOR_OP_SCALE, {.f32 = 2.0f} },
    
    // Boolean property - no operand needed
    { PROP_DATA_VALUES_EXACT, BEHAVIOR_OP_DESTROY, {0} },
};

static const FilterContract_t gain_contract = {
    .input_constraints = NULL,  // No specific input requirements
    .n_input_constraints = 0,
    .output_behaviors = gain_behaviors,
    .n_output_behaviors = ARRAY_SIZE(gain_behaviors),
};
```

### Signal Generator

```c
// No input constraints for source filter
static const OutputBehavior_t signal_gen_behaviors[] = {
    // Set all timing properties
    { PROP_SAMPLE_RATE_FIXED, BEHAVIOR_OP_SET, {.u32 = 48000} },
    { PROP_MAX_JITTER_KNOWN, BEHAVIOR_OP_SET, {.u32 = 0} },
    { PROP_TIMESTAMP_PRESENT, BEHAVIOR_OP_SET, {.u32 = 1} }, // Boolean as 1
    
    // Set signal bounds
    { PROP_SIGNAL_MIN_BOUNDED, BEHAVIOR_OP_SET, {.f32 = -1.0f} },
    { PROP_SIGNAL_MAX_BOUNDED, BEHAVIOR_OP_SET, {.f32 = 1.0f} },
    
    // Mathematical properties
    { PROP_GAIN_KNOWN, BEHAVIOR_OP_SET, {.f32 = 1.0f} },
    { PROP_DC_OFFSET_KNOWN, BEHAVIOR_OP_SET, {.f32 = 0.0f} },
};

static const FilterContract_t signal_gen_contract = {
    .input_constraints = NULL,  // Source filter - no inputs
    .n_input_constraints = 0,
    .output_behaviors = signal_gen_behaviors,
    .n_output_behaviors = ARRAY_SIZE(signal_gen_behaviors),
};
```

## Remaining Concerns and Implementation Considerations

### 1. Type Safety with Overloaded Union

The operand union in both constraints and behaviors can lead to type mismatches:

```c
// Potential error - wrong union member access
OutputBehavior_t behavior = {
    PROP_SAMPLE_RATE_FIXED,
    BEHAVIOR_OP_SCALE,
    {.f32 = 2.0}  // But SAMPLE_RATE is u32!
};
```

**Mitigation**: Since numeric values inherit type from the filter's data type, the property application code must be aware of both the property's natural type and the filter's data type.

### 2. Memory Management

Static vs dynamic allocation of constraint/behavior arrays:

```c
// Option 1: Static (compile-time)
static const InputConstraint_t constraints[] = {...};

// Option 2: Dynamic (runtime)
filter->contract.input_constraints = malloc(n * sizeof(InputConstraint_t));
```

**Recommendation**: Use static arrays for most filters to avoid memory management complexity.

### 3. Debugging and Error Reporting

When validation fails, providing meaningful error messages:

```c
typedef struct {
    char error_msg[256];
    InputConstraint_t* failed_constraint;
    PropertyValue_t actual_value;
    PropertyValue_t expected_value;
} ValidationContext_t;
```

### 4. Performance Considerations

- Validation happens at connection time (one-time cost)
- Property propagation could be cached for static pipelines
- Operator dispatch via switch statements is efficient

### 5. Order Independence

Since each filter should only affect a property once, behavior order shouldn't matter:
- A filter that scales signal bounds does only that
- Another filter might destroy timing properties
- No filter should both scale and clamp the same property

### 6. ADAPT Operator Implementation

The ADAPT operator needs careful design:

```c
// Example: Output type adapts to input type
{ PROP_OUTPUT_TYPE, BEHAVIOR_OP_ADAPT, {
    .adapt_fn = adapt_to_input_type
}}
```

### 7. Multi-Input Filters

For filters with multiple inputs requiring different constraints:

```c
// Input group constraints
struct {
    uint32_t input_mask;  // 0b011 = inputs 0 and 1
    InputConstraint_t* constraints;
} input_groups[] = {
    { 0b011, timing_constraints },  // Inputs 0,1 must have same timing
    { 0b100, reference_constraints } // Input 2 is reference signal
};
```

## Benefits of Operator-Based Design

1. **Expressiveness**: Can represent complex relationships between properties
2. **Composability**: Behaviors combine naturally without conflicts
3. **Extensibility**: Easy to add new operators without breaking existing code
4. **Type Safety**: Property classes prevent nonsensical operations
5. **Performance**: Simple array iteration with switch statements
6. **Debugging**: Each operation is explicit and traceable

## Implementation Strategy

### Phase 1: Core Infrastructure
1. Define property enumeration and metadata
2. Implement constraint and behavior operators
3. Create validation and propagation algorithms

### Phase 2: Filter Migration
1. Convert existing filters to use contracts
2. Add validation to connection logic
3. Implement error reporting

### Phase 3: Advanced Features
1. Multi-input constraint groups
2. Dynamic property adaptation
3. Performance optimizations

## Summary

The operator-based property system provides a flexible foundation for expressing filter requirements and transformations. By using operators for both input validation and output transformation, the system can handle complex property relationships while remaining efficient and understandable.

Key design decisions:
- **Single Responsibility**: Each filter affects each property at most once
- **Array-Based Storage**: Properties stored in array indexed by property enum for clean access
- **Type Safety**: Each property has a defined type stored with its value
- **Inheritance by Default**: Properties flow through filters unless explicitly modified
- **Static Declaration**: Most contracts can be defined at compile time
- **Clear Semantics**: Operators have well-defined behavior

The combination of:
1. Input constraints as validation operators
2. Output behaviors as transformation operators  
3. Type-safe property table with inheritance
4. Clean array-based access patterns

...creates a system that eliminates ambiguity in filter connections and enables automated compatibility checking, making the bpipe2 framework more robust and easier to use.
