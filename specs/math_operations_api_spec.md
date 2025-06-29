# Math Operations API Specification

## Overview

This specification defines the API patterns for mathematical operations in the bpipe framework, establishing conventions for single-input constant operations and multi-input operations.

## Design Principles

1. **Consistency**: All math operations follow predictable patterns
2. **Type Safety**: Operations validate and handle type conversions explicitly
3. **Zero-Copy**: Operations work in-place where possible
4. **Composability**: Operations can be chained without special handling
5. **Performance**: SIMD-friendly memory layouts and algorithms

## Naming Conventions

### Single Input + Constant Operations

**Pattern**: `Bp<Operation>Const`

These operations apply a mathematical function between an input stream and one or more constant values.

### Multi-Input Operations

**Pattern**: `Bp<Operation>Multi`

These operations apply element-wise functions across multiple synchronized input streams.

## API Patterns

### Structure Definitions

```c
// Single input + constant pattern
typedef struct {
    Bp_Filter_t base;           // Base filter (must be first)
    float constant;             // Operation-specific constant(s)
    // Additional constants as needed
} Bp<Operation>Const_t;

// Multi-input pattern
typedef struct {
    Bp_Filter_t base;           // Base filter (must be first)
    //  is this not captured by n_sources in the base object?
    size_t n_inputs;            // Number of inputs (2-8)
    // Operation-specific fields
} Bp<Operation>Multi_t;
```

### Initialization Functions

```c
// Single input + constant pattern
Bp_EC Bp<Operation>Const_Init(
    Bp<Operation>Const_t* op,
    float constant,                    // Operation-specific params
    const BpFilterConfig* config       // Standard filter config
);

// Multi-input pattern
Bp_EC Bp<Operation>Multi_Init(
    Bp<Operation>Multi_t* op,
    size_t n_inputs,                   // Number of inputs
    const BpFilterConfig* config       // Standard filter config
);
```

### Transform Functions

All operations use the standard transform signature:
```c
void Bp<Operation>Transform(
    Bp_Filter_t* filter,
    Bp_Batch_t** input_batches,
    int n_inputs,
    Bp_Batch_t* const* output_batches,
    int n_outputs
);
```

## Const Operations API

### BpAddConst

Adds a constant value to each sample.

```c
typedef struct {
    Bp_Filter_t base;
    float offset;
} BpAddConst_t;

Bp_EC BpAddConst_Init(
    BpAddConst_t* op,
    float offset,
    const BpFilterConfig* config
);

// Usage: output[i] = input[i] + offset
```

### BpMultiplyConst

Multiplies each sample by a constant value.

```c
typedef struct {
    Bp_Filter_t base;
    float scale;
} BpMultiplyConst_t;

Bp_EC BpMultiplyConst_Init(
    BpMultiplyConst_t* op,
    float scale,
    const BpFilterConfig* config
);

// Usage: output[i] = input[i] * scale
```

### BpScaleOffsetConst

Applies linear transformation y = ax + b.

```c
typedef struct {
    Bp_Filter_t base;
    float scale;    // 'a' coefficient
    float offset;   // 'b' coefficient
} BpScaleOffsetConst_t;

Bp_EC BpScaleOffsetConst_Init(
    BpScaleOffsetConst_t* op,
    float scale,
    float offset,
    const BpFilterConfig* config
);

// Usage: output[i] = input[i] * scale + offset
```

### BpClampConst

Clamps values to a range [min, max].

```c
typedef struct {
    Bp_Filter_t base;
    float min_val;
    float max_val;
} BpClampConst_t;

Bp_EC BpClampConst_Init(
    BpClampConst_t* op,
    float min_val,
    float max_val,
    const BpFilterConfig* config
);

// Usage: output[i] = clamp(input[i], min_val, max_val)
```

## Multi Operations API

### BpAddMulti

Element-wise addition of N inputs.

```c
typedef struct {
    Bp_Filter_t base;
    size_t n_inputs;    // 2-8 inputs
} BpAddMulti_t;

Bp_EC BpAddMulti_Init(
    BpAddMulti_t* op,
    size_t n_inputs,
    const BpFilterConfig* config
);

// Usage: output[i] = input0[i] + input1[i] + ... + inputN[i]
```

### BpMultiplyMulti

Element-wise multiplication of N inputs.

```c
typedef struct {
    Bp_Filter_t base;
    size_t n_inputs;    // 2-8 inputs
} BpMultiplyMulti_t;

Bp_EC BpMultiplyMulti_Init(
    BpMultiplyMulti_t* op,
    size_t n_inputs,
    const BpFilterConfig* config
);

// Usage: output[i] = input0[i] * input1[i] * ... * inputN[i]
```

### BpMixMulti

Weighted sum of N inputs with per-input coefficients.

```c
typedef struct {
    Bp_Filter_t base;
    size_t n_inputs;                // 2-8 inputs
    float weights[BP_MAX_INPUTS];   // Mixing coefficients
} BpMixMulti_t;

Bp_EC BpMixMulti_Init(
    BpMixMulti_t* op,
    size_t n_inputs,
    const float* weights,           // Array of n_inputs weights
    const BpFilterConfig* config
);

// Usage: output[i] = Σ(inputN[i] * weights[N])
```

### BpMinMulti / BpMaxMulti

Element-wise minimum/maximum across inputs.

```c
typedef struct {
    Bp_Filter_t base;
    size_t n_inputs;    // 2-8 inputs
} BpMinMulti_t;

typedef struct {
    Bp_Filter_t base;
    size_t n_inputs;    // 2-8 inputs
} BpMaxMulti_t;

Bp_EC BpMinMulti_Init(
    BpMinMulti_t* op,
    size_t n_inputs,
    const BpFilterConfig* config
);

Bp_EC BpMaxMulti_Init(
    BpMaxMulti_t* op,
    size_t n_inputs,
    const BpFilterConfig* config
);

// Usage: output[i] = min/max(input0[i], input1[i], ..., inputN[i])
```

## Error Handling

All operations handle these error conditions:

```c
// Initialization errors
Bp_EC_INVALID_CONFIG     // Invalid parameters (e.g., n_inputs > 8)
Bp_EC_CONFIG_REQUIRED    // Missing required config
Bp_EC_MUTEX_INIT_FAIL    // System resource failure

// Runtime errors  
Bp_EC_DTYPE_MISMATCH     // Input types don't match
Bp_EC_WIDTH_MISMATCH     // Batch sizes don't match
Bp_EC_NOINPUT            // Missing required input
```

## Type Handling

### Type Compatibility Matrix

| Operation | Float | Int | Unsigned |
|-----------|-------|-----|----------|
| Add       | ✓     | ✓   | ✓        |
| Multiply  | ✓     | ✓   | ✓        |
| Min/Max   | ✓     | ✓   | ✓        |
| Clamp     | ✓     | ✓   | ✓        |

### Type Conversion Rules

1. All inputs to a Multi operation must have the same dtype
2. Output dtype matches input dtype (no implicit conversions)
3. Constants are converted to match input dtype at init time
4. Overflow behavior follows C semantics for integer types

## Usage Examples

### Example 1: Simple Scaling

```c
// Scale signal by 2.5
BpMultiplyConst_t scaler;
BpFilterConfig config = BP_FILTER_CONFIG_DEFAULT;
config.dtype = DTYPE_FLOAT;

BpMultiplyConst_Init(&scaler, 2.5f, &config);
```

### Example 2: Multi-Input Multiplication

```c
// Multiply three signals together
BpMultiplyMulti_t mult;
BpFilterConfig config = BP_FILTER_CONFIG_DEFAULT;
config.dtype = DTYPE_FLOAT;
config.n_inputs = 3;

BpMultiplyMulti_Init(&mult, 3, &config);
```

### Example 3: Audio Mixing

```c
// Mix 4 audio channels with different volumes
BpMixMulti_t mixer;
float volumes[4] = {0.8f, 0.6f, 1.0f, 0.4f};
BpFilterConfig config = BP_FILTER_CONFIG_DEFAULT;
config.dtype = DTYPE_FLOAT;
config.n_inputs = 4;

BpMixMulti_Init(&mixer, 4, volumes, &config);
```

### Example 4: Pipeline Composition

```c
// Input → Scale → Add Offset → Clamp → Output
BpMultiplyConst_t scale;
BpAddConst_t offset;
BpClampConst_t clamp;

BpMultiplyConst_Init(&scale, 10.0f, &config);
BpAddConst_Init(&offset, -5.0f, &config);
BpClampConst_Init(&clamp, -1.0f, 1.0f, &config);

// Connect: source → scale → offset → clamp → sink
```

## Performance Considerations

### Memory Layout

- Operations work on contiguous arrays for cache efficiency
- SIMD-friendly alignment (16-byte boundaries)
- In-place operations where possible

### Optimization Opportunities

1. **Vectorization**: Operations should be written to allow compiler auto-vectorization
2. **Loop Unrolling**: For fixed n_inputs cases (2, 4, 8)
3. **Specialized Paths**: Fast paths for common cases (e.g., scale by power of 2)

### Benchmarking Targets

| Operation       | Target Throughput (samples/sec) |
|----------------|----------------------------------|
| Const ops      | 1G+ (single core)               |
| Multi ops (2)  | 500M+ (single core)             |
| Multi ops (8)  | 200M+ (single core)             |

## Future Extensions

1. **Complex Number Support**: Operations on complex float/double
2. **Vector Operations**: Dot product, cross product for multi-channel data
3. **Statistical Operations**: Running mean, variance, etc.
4. **Trigonometric Operations**: Sin, cos, atan2 for phase calculations
5. **Conditional Operations**: Select, threshold, comparisons

## Implementation Phases

### Phase 1: Foundation (Week 1)
- BpMultiplyConst
- BpMultiplyMulti
- Basic test infrastructure

### Phase 2: Core Operations (Week 2)
- BpAddConst, BpScaleOffsetConst, BpClampConst
- BpAddMulti, BpMixMulti
- BpMinMulti, BpMaxMulti

### Phase 3: Integration (Week 3)
- Integration with synchronizers
- Performance optimization
- Documentation and examples


## BF general comments
- No need to duplicate the `number_of_input_filters` feild in `BpFilterConfig`.
