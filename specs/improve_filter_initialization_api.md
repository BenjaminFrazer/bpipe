# Filter Initialization API Improvement Plan

## Problem Statement

The current filter initialization API is error-prone and has led to intermittent memory corruption bugs. The deferred buffer allocation pattern requires developers to manually manage the correct initialization sequence, leading to subtle but serious runtime failures.

### Current Problematic Pattern
```c
// Error-prone: Type set after allocation
BpFilter_Init(&filter, transform, 0, 128, 64, 6, 1);
filter.dtype = DTYPE_FLOAT;           // Easy to forget or do wrong
filter.data_width = sizeof(float);    // Can be inconsistent
Bp_allocate_buffers(&filter, 0);      // May allocate wrong size
```

### Issues Identified
1. **Memory corruption** - Buffer allocation with undefined type leads to size mismatches
2. **Runtime failures** - Type mismatches only discovered when data flows
3. **Debugging difficulty** - Intermittent failures are hard to reproduce and trace
4. **Cognitive overhead** - Complex initialization sequence is error-prone
5. **API inconsistency** - Some components (like SignalGen) handle this internally, others don't

## Proposed Solution

Move to configuration-based initialization with automatic buffer allocation and connection-time type checking. This approach uses a configuration struct to provide a more extensible and maintainable API.

### New API Design

#### 1. Configuration Structure
```c
// Configuration structure for filter initialization
typedef struct {
    TransformFcn_t transform;
    SampleDtype_t dtype;
    size_t buffer_size;
    int batch_size;
    int number_of_batches_exponent;
    int number_of_input_filters;
    
    // Future extensibility without breaking API
    OverflowBehaviour_t overflow_behaviour;  // Default: OVERFLOW_BLOCK
    bool auto_allocate_buffers;             // Default: true
    void* memory_pool;                      // Default: NULL (use malloc)
    size_t alignment;                       // Default: 0 (natural alignment)
} BpFilterConfig;

// Default configuration helper
#define BP_FILTER_CONFIG_DEFAULT { \
    .dtype = DTYPE_NDEF, \
    .buffer_size = 128, \
    .batch_size = 64, \
    .number_of_batches_exponent = 6, \
    .number_of_input_filters = 1, \
    .overflow_behaviour = OVERFLOW_BLOCK, \
    .auto_allocate_buffers = true, \
    .memory_pool = NULL, \
    .alignment = 0 \
}
```

#### 2. Unified Initialization Function
```c
// Single initialization function for all filter types
Bp_EC BpFilter_InitFromConfig(Bp_Filter_t* filter, const BpFilterConfig* config);

// Usage examples:
// Basic initialization
BpFilterConfig config = {
    .transform = BpPassThroughTransform,
    .dtype = DTYPE_FLOAT,
    .buffer_size = 128,
    .batch_size = 64,
    .number_of_batches_exponent = 6,
    .number_of_input_filters = 1
};
BpFilter_InitFromConfig(&filter, &config);

// Using defaults with designated initializers
BpFilterConfig config2 = BP_FILTER_CONFIG_DEFAULT;
config2.transform = MyCustomTransform;
config2.dtype = DTYPE_INT;
BpFilter_InitFromConfig(&filter2, &config2);

// Minimal specification
BpFilter_InitFromConfig(&filter3, &(BpFilterConfig){
    .transform = BpPassThroughTransform,
    .dtype = DTYPE_FLOAT
});
```

#### 3. Configuration Helpers
```c
// Predefined configurations for common use cases
extern const BpFilterConfig BP_CONFIG_FLOAT_STANDARD;
extern const BpFilterConfig BP_CONFIG_INT_STANDARD;
extern const BpFilterConfig BP_CONFIG_HIGH_THROUGHPUT;
extern const BpFilterConfig BP_CONFIG_LOW_LATENCY;

// Configuration validation
Bp_EC BpFilterConfig_Validate(const BpFilterConfig* config);

// Configuration from legacy API for migration
BpFilterConfig BpFilterConfig_FromLegacy(
    TransformFcn_t transform,
    int initial_state,
    size_t buffer_size,
    int batch_size,
    int number_of_batches_exponent,
    int number_of_input_filters);
```

#### 4. Connection-Time Type Checking
```c
// Enhanced with better error reporting
typedef struct {
    Bp_EC code;
    const char* message;
    SampleDtype_t expected_type;
    SampleDtype_t actual_type;
} BpTypeError;

Bp_EC Bp_add_sink_with_error(Bp_Filter_t* source, Bp_Filter_t* sink, 
                             BpTypeError* error);

// Backward compatible wrapper
Bp_EC Bp_add_sink(Bp_Filter_t* source, Bp_Filter_t* sink) {
    return Bp_add_sink_with_error(source, sink, NULL);
}
```

#### 5. Specialized Component Initialization
```c
// Signal generator uses config pattern too
typedef struct {
    BpWaveform_t waveform;
    float frequency;
    float amplitude;
    float phase;
    float x_offset;
    BpFilterConfig base_config;  // Embedded base configuration
} BpSignalGenConfig;

Bp_EC BpSignalGen_InitFromConfig(Bp_SignalGen_t* gen, 
                                 const BpSignalGenConfig* config);
```

## Implementation Plan

### Phase 1: Core API Implementation
- [ ] Define `BpFilterConfig` structure in `bpipe/core.h`
- [ ] Implement `BpFilter_InitFromConfig` function in `bpipe/core.c`
- [ ] Implement `BpFilterConfig_Validate` for configuration validation
- [ ] Add automatic buffer allocation within initialization based on config
- [ ] Create predefined configurations (BP_CONFIG_FLOAT_STANDARD, etc.)
- [ ] Update `Bp_EC` enum with new error types

### Phase 2: Enhanced Error Handling and Type Checking
- [ ] Define `BpTypeError` structure for detailed error reporting
- [ ] Implement `Bp_add_sink_with_error` with enhanced type checking
- [ ] Update existing `Bp_add_sink` to use new implementation
- [ ] Add validation for config struct fields (buffer sizes, exponents)
- [ ] Implement debug logging for initialization and connections
- [ ] Create `BpFilterConfig_FromLegacy` for migration support

### Phase 3: Migration and Testing
- [ ] Update existing tests to use config-based initialization
- [ ] Add unit tests for configuration validation
- [ ] Add unit tests for type checking with error reporting
- [ ] Create integration tests for multi-filter type scenarios
- [ ] Add stress tests to verify memory safety with various configs
- [ ] Test backward compatibility with legacy initialization

### Phase 4: Component Updates
- [ ] Define `BpSignalGenConfig` structure
- [ ] Implement `BpSignalGen_InitFromConfig`
- [ ] Update Python bindings to support config-based initialization
- [ ] Create Python helper classes for configuration
- [ ] Update all example code to use new API
- [ ] Add configuration examples to documentation

### Phase 5: Deprecation and Cleanup
- [ ] Mark old `BpFilter_Init` as deprecated with compiler warnings
- [ ] Make `Bp_allocate_buffers` static/internal-only
- [ ] Create comprehensive migration guide
- [ ] Add static analysis rules to detect old patterns
- [ ] Remove or hide legacy initialization from public headers

## Benefits

### Immediate Benefits
1. **Memory safety** - Eliminates initialization-time buffer corruption
2. **Fail-fast errors** - Type mismatches caught at connection time with detailed error info
3. **Simplified API** - Single initialization function with clear, named parameters
4. **Better debugging** - Configuration validation and detailed error messages
5. **Future-proof** - New fields can be added without breaking existing code

### Long-term Benefits
1. **Maintainability** - Self-documenting configuration structures
2. **Reusability** - Common configurations can be shared and reused
3. **Extensibility** - Easy to add new parameters without API proliferation
4. **Performance** - Configurations can be validated once and reused
5. **Testability** - Configurations can be tested independently of initialization

## Risk Analysis

### Low Risk
- **Backward compatibility** - Old API can coexist during transition
- **Performance impact** - Minimal, mostly moves existing work to init time
- **Testing complexity** - New API is simpler to test

### Medium Risk
- **Migration effort** - Existing code needs updates
- **API learning curve** - Developers need to learn new patterns

### Mitigation Strategies
- **Gradual rollout** - Implement alongside existing API
- **Comprehensive testing** - Extensive unit and integration tests
- **Clear documentation** - Migration guide and examples
- **Automated validation** - Static analysis to catch old patterns

## Success Criteria

1. **Zero memory corruption** - All buffer allocation issues eliminated
2. **Type safety** - All type mismatches caught at connection time
3. **API consistency** - All filter types use same initialization pattern
4. **Test coverage** - 100% coverage of new initialization paths
5. **Performance parity** - No regression in runtime performance
6. **Developer experience** - Simplified, less error-prone API

## Implementation Notes

### Error Code Extensions
```c
typedef enum _Bp_EC {
    // ... existing codes ...
    Bp_EC_DTYPE_MISMATCH = -12,     // Source/sink data types don't match
    Bp_EC_WIDTH_MISMATCH = -13,     // Data width mismatch
    Bp_EC_INVALID_DTYPE = -14,      // Invalid or unsupported data type
    Bp_EC_INVALID_CONFIG = -15,     // Invalid configuration parameters
    Bp_EC_CONFIG_REQUIRED = -16,    // Configuration missing required fields
} Bp_EC;
```

### Configuration Implementation Details
```c
// Internal helper to apply defaults to partial configs
static void BpFilterConfig_ApplyDefaults(BpFilterConfig* config) {
    if (config->buffer_size == 0) config->buffer_size = 128;
    if (config->batch_size == 0) config->batch_size = 64;
    if (config->number_of_batches_exponent == 0) {
        config->number_of_batches_exponent = 6;
    }
    if (config->number_of_input_filters == 0) {
        config->number_of_input_filters = 1;
    }
    // auto_allocate_buffers defaults to true
    // overflow_behaviour defaults to OVERFLOW_BLOCK (0)
}

// Example predefined configuration
const BpFilterConfig BP_CONFIG_FLOAT_STANDARD = {
    .dtype = DTYPE_FLOAT,
    .buffer_size = 128,
    .batch_size = 64,
    .number_of_batches_exponent = 6,
    .number_of_input_filters = 1,
    .overflow_behaviour = OVERFLOW_BLOCK,
    .auto_allocate_buffers = true,
    .memory_pool = NULL,
    .alignment = 0
};
```

### Migration Strategy
```c
// Wrapper for gradual migration
Bp_EC BpFilter_Init_Legacy(Bp_Filter_t* filter, 
                          TransformFcn_t transform,
                          int initial_state,
                          size_t buffer_size,
                          int batch_size,
                          int number_of_batches_exponent,
                          int number_of_input_filters) {
    BpFilterConfig config = BpFilterConfig_FromLegacy(
        transform, initial_state, buffer_size, 
        batch_size, number_of_batches_exponent, 
        number_of_input_filters);
    
    // Log deprecation warning
    #ifdef BP_WARN_DEPRECATED
    fprintf(stderr, "Warning: BpFilter_Init is deprecated. "
                    "Use BpFilter_InitFromConfig instead.\n");
    #endif
    
    return BpFilter_InitFromConfig(filter, &config);
}
```

### Testing Strategy
- Unit tests for configuration validation
- Unit tests for default application
- Integration tests for type checking with detailed errors
- Migration tests ensuring legacy API still works
- Stress tests with various configuration combinations
- Performance benchmarks comparing old vs new initialization
- Fuzz testing for configuration edge cases

## Timeline

- **Week 1-2**: Core API implementation and basic testing
- **Week 3**: Enhanced error handling and validation
- **Week 4**: Migration of existing tests and code
- **Week 5**: Integration testing and performance validation
- **Week 6**: Documentation and final testing

## Conclusion

This configuration-based API improvement addresses the fundamental memory corruption issues while providing superior extensibility compared to adding more initialization functions. The configuration struct approach offers:

1. **Single API surface** - One initialization function handles all cases
2. **Self-documenting code** - Named fields clarify intent
3. **Future extensibility** - New fields don't break existing code
4. **Better ergonomics** - Partial specification with defaults
5. **Reusable patterns** - Common configurations can be shared

The fail-fast type checking combined with detailed error reporting will catch issues at connection time rather than during data flow, significantly reducing debugging time.

The gradual migration approach with legacy wrappers ensures zero disruption to existing code while encouraging adoption of the safer API. This design positions the framework for long-term maintainability and feature growth without API proliferation.