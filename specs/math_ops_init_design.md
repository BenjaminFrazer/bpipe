# Math Operations Initialization Design

## Overview

This document proposes a unified initialization approach for math operations that:
1. Minimizes code duplication between operation types
2. Provides clear, consistent APIs
3. Maps cleanly to Python with minimal wrapper code
4. Enables future extensibility without breaking changes

## Core Design Principles

### 1. Operation-Specific Config Structures

Each operation type has its own config structure that embeds the base filter config:

```c
// Base pattern for all math operations
typedef struct {
    BpFilterConfig base_config;  // Standard filter configuration
    // Operation-specific parameters follow...
} Bp<Operation>Config;
```

### 2. Unified Initialization Pattern

All operations follow the same init pattern:

```c
Bp_EC Bp<Operation>_Init(
    void* operation,                    // Polymorphic operation struct
    const Bp<Operation>Config* config   // Operation-specific config
);
```

## Configuration Structures

### Base Math Operation Config

```c
// Common fields for all math operations
typedef struct {
    BpFilterConfig base_config;     // Inherits all filter settings
    bool in_place;                  // If true, reuse input buffer for output
    bool check_overflow;            // Enable overflow checking (performance cost)
    size_t simd_alignment;          // SIMD alignment hint (0 = auto)
} BpMathOpConfig;

// Default for math operations
#define BP_MATH_OP_CONFIG_DEFAULT { \
    .base_config = BP_FILTER_CONFIG_DEFAULT, \
    .in_place = false, \
    .check_overflow = false, \
    .simd_alignment = 0 \
}
```

### Const Operation Configs

```c
// Single constant operations
typedef struct {
    BpMathOpConfig math_config;     // Inherits math operation settings
    float value;                    // The constant value
} BpUnaryConstConfig;

// Dual constant operations (e.g., scale + offset)
typedef struct {
    BpMathOpConfig math_config;
    float value1;                   // First constant
    float value2;                   // Second constant
} BpBinaryConstConfig;

// Examples:
typedef BpUnaryConstConfig BpAddConstConfig;        // value = offset
typedef BpUnaryConstConfig BpMultiplyConstConfig;   // value = scale
typedef BpBinaryConstConfig BpScaleOffsetConstConfig; // value1 = scale, value2 = offset
typedef BpBinaryConstConfig BpClampConstConfig;      // value1 = min, value2 = max
```

### Multi Operation Configs

```c
// Basic multi-input operations
typedef struct {
    BpMathOpConfig math_config;
    size_t n_inputs;                // Number of inputs (2-8)
} BpMultiOpConfig;

// Multi-input with weights
typedef struct {
    BpMathOpConfig math_config;
    size_t n_inputs;
    float weights[BP_MAX_INPUTS];   // Per-input weights
} BpWeightedMultiOpConfig;

// Examples:
typedef BpMultiOpConfig BpAddMultiConfig;
typedef BpMultiOpConfig BpMultiplyMultiConfig;
typedef BpWeightedMultiOpConfig BpMixMultiConfig;
```

## Initialization Functions

### Generic Initialization Helper

```c
// Internal helper that handles common initialization
static Bp_EC BpMathOp_InitCommon(
    Bp_Filter_t* filter,
    const BpMathOpConfig* math_config,
    TransformFcn_t* transform
) {
    // Set the transform function
    BpFilterConfig config = math_config->base_config;
    config.transform = transform;
    
    // Initialize base filter
    Bp_EC ec = BpFilter_Init(filter, &config);
    if (ec != Bp_EC_OK) return ec;
    
    // Set math-specific properties
    // (stored in filter user_data or extended struct)
    
    return Bp_EC_OK;
}
```

### Const Operation Initialization

```c
// Generic const operation initializer
Bp_EC BpConstOp_Init(
    void* op_struct,
    const void* config,
    size_t config_size,
    TransformFcn_t* transform
) {
    Bp_Filter_t* filter = (Bp_Filter_t*)op_struct;
    const BpMathOpConfig* math_config = (const BpMathOpConfig*)config;
    
    return BpMathOp_InitCommon(filter, math_config, transform);
}

// Specific operation initializers
Bp_EC BpAddConst_Init(BpAddConst_t* op, const BpAddConstConfig* config) {
    op->offset = config->value;
    return BpConstOp_Init(op, config, sizeof(*config), BpAddConstTransform);
}

Bp_EC BpMultiplyConst_Init(BpMultiplyConst_t* op, const BpMultiplyConstConfig* config) {
    op->scale = config->value;
    return BpConstOp_Init(op, config, sizeof(*config), BpMultiplyConstTransform);
}
```

### Multi Operation Initialization

```c
// Generic multi operation initializer
Bp_EC BpMultiOp_Init(
    void* op_struct,
    const BpMultiOpConfig* config,
    TransformFcn_t* transform
) {
    Bp_Filter_t* filter = (Bp_Filter_t*)op_struct;
    
    // Validate n_inputs
    if (config->n_inputs < 2 || config->n_inputs > BP_MAX_INPUTS) {
        return Bp_EC_INVALID_CONFIG;
    }
    
    // Update base config for multiple inputs
    BpFilterConfig base_config = config->math_config.base_config;
    base_config.number_of_input_filters = config->n_inputs;
    
    return BpMathOp_InitCommon(filter, &config->math_config, transform);
}

// Specific operation initializers
Bp_EC BpAddMulti_Init(BpAddMulti_t* op, const BpAddMultiConfig* config) {
    op->n_inputs = config->n_inputs;
    return BpMultiOp_Init(op, config, BpAddMultiTransform);
}
```

## Python Wrapper Design

### Python Config Classes

```python
from dataclasses import dataclass
from typing import Optional

@dataclass
class BpMathOpConfig:
    """Base configuration for all math operations"""
    dtype: str = "float"
    batch_size: int = 64
    buffer_size: int = 128
    in_place: bool = False
    check_overflow: bool = False
    
    def to_c_config(self):
        """Convert to C configuration structure"""
        return {
            'dtype': DTYPE_MAP[self.dtype],
            'batch_size': self.batch_size,
            'buffer_size': self.buffer_size,
            'in_place': self.in_place,
            'check_overflow': self.check_overflow
        }

@dataclass
class BpConstOpConfig(BpMathOpConfig):
    """Configuration for const operations"""
    value: float = 0.0

@dataclass 
class BpScaleOffsetConfig(BpMathOpConfig):
    """Configuration for scale+offset operations"""
    scale: float = 1.0
    offset: float = 0.0

@dataclass
class BpMultiOpConfig(BpMathOpConfig):
    """Configuration for multi-input operations"""
    n_inputs: int = 2
```

### Python Wrapper Pattern

```python
class BpMathOp:
    """Base class for all math operations"""
    
    def __init__(self, config: BpMathOpConfig):
        self.config = config
        self._c_filter = None
        self._init_c_filter()
    
    def _init_c_filter(self):
        """Initialize the C filter - overridden by subclasses"""
        raise NotImplementedError

class BpAddConst(BpMathOp):
    """Add constant operation"""
    
    def __init__(self, offset: float, **kwargs):
        config = BpConstOpConfig(value=offset, **kwargs)
        super().__init__(config)
    
    def _init_c_filter(self):
        # Call C initializer with config
        c_config = self.config.to_c_config()
        self._c_filter = _bpipe.BpAddConst_Init(c_config)

class BpMultiplyMulti(BpMathOp):
    """Multiply multiple inputs"""
    
    def __init__(self, n_inputs: int = 2, **kwargs):
        config = BpMultiOpConfig(n_inputs=n_inputs, **kwargs)
        super().__init__(config)
    
    def _init_c_filter(self):
        c_config = self.config.to_c_config()
        self._c_filter = _bpipe.BpMultiplyMulti_Init(c_config)
```

### Python Factory Pattern

```python
class BpMathOps:
    """Factory for creating math operations"""
    
    @staticmethod
    def add_const(offset: float, **kwargs) -> BpAddConst:
        """Create an add constant operation"""
        return BpAddConst(offset, **kwargs)
    
    @staticmethod
    def multiply_const(scale: float, **kwargs) -> BpMultiplyConst:
        """Create a multiply constant operation"""
        return BpMultiplyConst(scale, **kwargs)
    
    @staticmethod
    def scale_offset(scale: float, offset: float, **kwargs) -> BpScaleOffset:
        """Create a scale+offset operation"""
        return BpScaleOffset(scale, offset, **kwargs)
    
    @staticmethod
    def add_multi(n_inputs: int = 2, **kwargs) -> BpAddMulti:
        """Create a multi-input addition"""
        return BpAddMulti(n_inputs, **kwargs)

# Usage examples:
add5 = BpMathOps.add_const(5.0)
scale2 = BpMathOps.multiply_const(2.0)
mixer = BpMathOps.mix_multi([0.5, 0.3, 0.2])
```

## Benefits of This Approach

### 1. Minimal Duplication
- Common initialization logic in `BpMathOp_InitCommon`
- Config structures use composition, not duplication
- Python base classes handle common functionality

### 2. Maximum Clarity
- Each operation has a specific config struct
- Clear naming: `Bp<Operation>Config` → `Bp<Operation>_Init`
- Python API mirrors C API structure

### 3. Easy Python Wrapping
- Config structures map directly to Python dataclasses
- Standard pattern for all operations
- Factory pattern provides clean Python API

### 4. Extensibility
- New operations just need new config + init function
- Base functionality inherited automatically
- Python wrappers follow same pattern

## Migration Path

For existing code, we can provide compatibility shims:

```c
// Old API
Bp_EC BpAddConst_Init_Legacy(BpAddConst_t* op, float offset, 
                             size_t buffer_size, int batch_size) {
    BpAddConstConfig config = {
        .math_config = BP_MATH_OP_CONFIG_DEFAULT,
        .value = offset
    };
    config.math_config.base_config.buffer_size = buffer_size;
    config.math_config.base_config.batch_size = batch_size;
    return BpAddConst_Init(op, &config);
}
```

## Example: Complete Implementation

Here's how `BpMultiplyConst` would look:

```c
// In bpipe/math_ops.h
typedef struct {
    Bp_Filter_t base;
    float scale;
} BpMultiplyConst_t;

typedef struct {
    BpMathOpConfig math_config;
    float value;  // The scale factor
} BpMultiplyConstConfig;

Bp_EC BpMultiplyConst_Init(BpMultiplyConst_t* op, const BpMultiplyConstConfig* config);

// In bpipe/math_ops.c
void BpMultiplyConstTransform(Bp_Filter_t* filter, Bp_Batch_t** inputs, 
                             int n_inputs, Bp_Batch_t* const* outputs, int n_outputs) {
    BpMultiplyConst_t* op = (BpMultiplyConst_t*)filter;
    Bp_Batch_t* in = inputs[0];
    Bp_Batch_t* out = outputs[0];
    
    // Copy batch metadata
    *out = *in;
    
    // Apply operation based on dtype
    switch (in->dtype) {
        case DTYPE_FLOAT: {
            float* in_data = (float*)in->data;
            float* out_data = (float*)out->data;
            for (size_t i = 0; i < in->tail - in->head; i++) {
                out_data[i] = in_data[i] * op->scale;
            }
            break;
        }
        // ... handle other types
    }
}

Bp_EC BpMultiplyConst_Init(BpMultiplyConst_t* op, const BpMultiplyConstConfig* config) {
    op->scale = config->value;
    return BpConstOp_Init(op, config, sizeof(*config), BpMultiplyConstTransform);
}
```

This design provides a clean, extensible foundation for all math operations while maintaining consistency with the existing bpipe architecture.