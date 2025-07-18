# Filter categories

## Overview

This document describes the fundimental types of filters.

## Core Philosophy

### Current Design Problems
1. The generic `Bp_Worker` tries to abstract away details that complex filters actually need
2. Simple filters are forced to deal with unnecessary complexity (multi-input/output arrays, batch management)
3. The transform function signature is awkward - passing arrays of batches when most filters only use one
4. Corner cases in multi-input/output scenarios challenge the specificity of the transform function's role

### New Design Principles
1. **Each filter owns its execution model** - The filter's worker function has complete control
2. **Simplicity by default** - Common patterns (like element-wise mapping) have simple APIs
3. **Power when needed** - Complex filters can implement custom synchronization, timing, and routing
4. **No forced abstractions** - Filters aren't required to fit into a one-size-fits-all execution model

## Architecture

### Core Filter Structure

### Worker Function Categories

#### 1. Map Worker (Simple Element-wise Operations)
For filters that process data element-by-element without complex synchronization needs:

```c
typedef void (*BpMapFunc)(const void* input, void* output, size_t n_samples);

void* BpMapWorker(void* filter_ptr) {
    Bp_Filter_t* f = (Bp_Filter_t*)filter_ptr;
    BpMapFunc map_func = (BpMapFunc)f->context;
    
    // Simple single-input, single-output processing
    while (f->running) {
        // Get available data
        // Call map function
        // Manage batch transitions
    }
    return NULL;
}
```

#### 2. Generic Worker (Current Bp_Worker)
For filters that need the full multi-input/output infrastructure:

```c
void* BpGenericWorker(void* filter_ptr) {
    // Current Bp_Worker implementation
    // Handles multiple inputs/outputs
    // Automatic distribution to multiple sinks
    // Complex batch management
}
```

#### 3. Stateful Map Worker (State-Preserving Element-wise Operations)
For filters that need to maintain state between processing iterations:

#### 4. Reframe Worker (Batch Size Conversion)
For filters that convert between different batch sizes:

```c
void* BpReframeWorker(void* filter_ptr) {
    Bp_Filter_t* f = (Bp_Filter_t*)filter_ptr;
    ReframeState* state = (ReframeState*)f->context;
    
    // Handles accumulation when output > input batch size
    // Handles distribution when output < input batch size
    // Maintains sample continuity across batch boundaries
    // Preserves timing information appropriately
    
    while (f->running) {
        // Get input data
        // Accumulate or distribute based on size ratio
        // Emit complete output batches
        // Handle partial batch state
    }
    return NULL;
}
```

Use cases:
- Adapting between components with different batch size requirements
- Downsampling data for reduced processing load
- Accumulating small batches for more efficient bulk processing
- Example: `Input(64) → Reframe → Output(256)` accumulates 4 input batches

#### 5. Function Generator Worker (Arbitrary Waveform Generation)
For source filters that generate arbitrary waveforms based on time:

#### 6. Custom Workers
For specialized filters with unique requirements:

```c
void* CustomSyncWorker(void* filter_ptr) {
    Bp_Filter_t* f = (Bp_Filter_t*)filter_ptr;
    
    // Custom synchronization logic
    // Direct buffer access
    // Specialized timing requirements
    // Custom error handling
}
```

## Implementation Patterns

### Creating Filters

#### Simple Map Filter
```c
// User writes this simple function
void scale_samples(const float* in, float* out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        out[i] = in[i] * 2.0f;
    }
}
```

The worker-centric design provides a cleaner, more flexible architecture that scales from simple element-wise operations to complex multi-input synchronization scenarios. By allowing each filter to own its execution model, we eliminate the impedance mismatch between what filters need and what the framework provides, resulting in simpler code for both users and maintainers.

## Filter Composition Patterns

### Batch Size Adaptation
When components have different batch size requirements, compose Tee with Reframe:

```
// Different outputs need different batch sizes
Input(128) → Tee → Output1(128)           // Direct, no conversion
                 → Reframe → Output2(256)  // Accumulate 2 batches
                 → Reframe → Output3(64)   // Split into 2 batches

// This is more efficient than building size conversion into Tee because:
// 1. Output1 has zero overhead (straight copy)
// 2. Only outputs needing conversion pay the cost
// 3. Each Reframe can be optimized for its specific ratio
```

### Multi-Stage Processing
Complex pipelines benefit from clear separation of concerns:

```
Source → Map(scale) → Tee → Map(filter1) → Sink1
                          → Reframe → Map(filter2) → Sink2
```

Each filter does exactly one thing, making the pipeline easy to understand, test, and optimize.

