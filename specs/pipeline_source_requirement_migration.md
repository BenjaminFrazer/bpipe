# Pipeline Source Requirement Migration Specification

## Executive Summary

This specification defines the migration path for enforcing that root pipelines must contain at least one source filter. This change improves system correctness by preventing incomplete pipeline configurations that cannot generate data.

## Problem Statement

Currently, the pipeline validation system attempts to validate pipelines without source filters by starting with UNKNOWN properties. This leads to:

1. **Validation failures** when filters like Tee require specific input properties but receive UNKNOWN
2. **Ambiguous semantics** about what a pipeline without sources represents
3. **Complex workarounds** attempting to synthesize valid properties for validation
4. **Confusion** between root pipelines (standalone systems) and nested pipelines (components)

## Solution Overview

Root pipelines (those with no external connections) must contain at least one source filter. Pipelines without sources can only exist as nested components within other pipelines.

## Migration Strategy

### Phase 1: Documentation and Warning (Week 1)
- Update all documentation to clarify the requirement
- Add deprecation warnings when root pipelines lack sources
- Provide migration guides with examples

### Phase 2: Validation Enhancement (Week 2)
- Implement source filter detection in `pipeline_validate_properties()`
- Return clear error messages for incomplete root pipelines
- Allow grace period with warning-only mode via flag

### Phase 3: Enforcement (Week 3)
- Make validation failures mandatory for root pipelines without sources
- Update all tests to comply with new requirements
- Remove deprecated patterns from examples

## Implementation Details

### 1. Source Filter Detection

```c
static bool pipeline_has_source_filters(const Pipeline_t* pipeline) {
    for (size_t i = 0; i < pipeline->n_filters; i++) {
        if (pipeline->filters[i]->n_inputs == 0) {
            return true;  // Found a source filter
        }
    }
    return false;
}
```

### 2. Validation Logic Update

```c
Bp_EC pipeline_validate_properties(const Pipeline_t* pipeline,
                                   PropertyTable_t* external_inputs,
                                   size_t n_external_inputs,
                                   char* error_msg, 
                                   size_t error_msg_size) {
    
    bool is_root_pipeline = (n_external_inputs == 0);
    
    if (is_root_pipeline) {
        // Check for source filters
        if (!pipeline_has_source_filters(pipeline)) {
            snprintf(error_msg, error_msg_size,
                    "Root pipeline '%s' has no source filters. "
                    "A root pipeline must contain at least one source filter "
                    "to generate data. See migration guide for solutions.",
                    pipeline->base.name);
            
            #ifdef PIPELINE_SOURCE_WARNING_ONLY
                fprintf(stderr, "WARNING: %s\n", error_msg);
                // Continue with validation using constrained properties
                // (temporary backward compatibility)
            #else
                return Bp_EC_INCOMPLETE_PIPELINE;  // New error code
            #endif
        }
        
        // Continue with normal validation from sources
        // ...
    } else {
        // Nested pipeline - sources optional
        // ...
    }
}
```

### 3. New Error Code

```c
// In bperr.h
typedef enum {
    // ... existing codes ...
    Bp_EC_INCOMPLETE_PIPELINE,  // Pipeline lacks required source filters
} Bp_EC;

// In error lookup table
[Bp_EC_INCOMPLETE_PIPELINE] = "Pipeline configuration incomplete - missing source filters"
```

## Migration Patterns

### Pattern 1: External Source to Internal Source

**Before (Invalid):**
```c
// External source
SignalGenerator_t sig_gen;
signal_generator_init(&sig_gen, config);

// Pipeline without sources
Filter_t* filters[] = {&tee.base, &map1.base, &map2.base};
Pipeline_t pipeline;
pipeline_init(&pipeline, (Pipeline_config_t){
    .filters = filters,
    .n_filters = 3,
    .input_filter = &tee.base,
    // ...
});

// Connect external source to pipeline
filt_sink_connect(&sig_gen.base, 0, pipeline.base.input_buffers[0]);
```

**After (Valid - Option A: Include source in pipeline):**
```c
// Initialize source
SignalGenerator_t sig_gen;
signal_generator_init(&sig_gen, config);

// Include source in pipeline
Filter_t* filters[] = {&sig_gen.base, &tee.base, &map1.base, &map2.base};
Pipeline_t pipeline;
pipeline_init(&pipeline, (Pipeline_config_t){
    .filters = filters,
    .n_filters = 4,
    .input_filter = &sig_gen.base,  // Now starts with source
    // ...
});

// No external connection needed - pipeline is self-contained
```

**After (Valid - Option B: Nested pipeline):**
```c
// Create inner pipeline (transform chain)
Filter_t* inner_filters[] = {&tee.base, &map1.base, &map2.base};
Pipeline_t inner_pipeline;
pipeline_init(&inner_pipeline, (Pipeline_config_t){
    .name = "transform_chain",
    .filters = inner_filters,
    .n_filters = 3,
    .input_filter = &tee.base,
    // ...
});

// Create outer pipeline with source
Filter_t* outer_filters[] = {&sig_gen.base, &inner_pipeline.base};
Pipeline_t outer_pipeline;
pipeline_init(&outer_pipeline, (Pipeline_config_t){
    .name = "complete_system",
    .filters = outer_filters,
    .n_filters = 2,
    // ...
});
```

### Pattern 2: Test Fixture Update

**Before (Invalid test):**
```c
void test_pipeline_dag_flow(void) {
    // Create pipeline without sources
    Tee_filt_t tee;
    Map_filt_t map1, map2;
    
    Filter_t* filters[] = {&tee.base, &map1.base, &map2.base};
    Pipeline_t pipeline;
    pipeline_init(&pipeline, config);
    
    // External source for testing
    SignalGenerator_t test_source;
    filt_sink_connect(&test_source.base, 0, pipeline.base.input_buffers[0]);
    
    // This will fail validation in new system
    CHECK_ERR(filt_start(&pipeline.base));  // ERROR: No source filters
}
```

**After (Valid test):**
```c
void test_pipeline_dag_flow(void) {
    // Create complete pipeline with source
    SignalGenerator_t source;
    Tee_filt_t tee;
    Map_filt_t map1, map2;
    
    signal_generator_init(&source, source_config);
    
    Filter_t* filters[] = {&source.base, &tee.base, &map1.base, &map2.base};
    Pipeline_t pipeline;
    pipeline_init(&pipeline, config);
    
    // No external connection needed
    CHECK_ERR(filt_start(&pipeline.base));  // OK: Has source filter
}
```

### Pattern 3: Reusable Transform Chains

**Before (Attempting to create reusable transform pipeline):**
```c
// Tried to create reusable transform pipeline
Pipeline_t* create_transform_pipeline(void) {
    // ... create filters ...
    Filter_t* filters[] = {&transform1.base, &transform2.base};
    
    Pipeline_t* pipeline = malloc(sizeof(Pipeline_t));
    pipeline_init(pipeline, config);  // No sources!
    return pipeline;
}
```

**After (Create factory function instead):**
```c
// Create a pipeline builder that adds transforms to existing pipeline
Bp_EC add_transform_chain(Pipeline_t* pipeline, 
                          Filter_t* input_connection_point) {
    // Create transform filters
    Transform1_t* t1 = malloc(sizeof(Transform1_t));
    Transform2_t* t2 = malloc(sizeof(Transform2_t));
    
    transform1_init(t1, config1);
    transform2_init(t2, config2);
    
    // Add to existing pipeline
    pipeline_add_filter(pipeline, &t1->base);
    pipeline_add_filter(pipeline, &t2->base);
    
    // Connect to specified input point
    pipeline_add_connection(pipeline, input_connection_point, 0, &t1->base, 0);
    pipeline_add_connection(pipeline, &t1->base, 0, &t2->base, 0);
    
    return Bp_EC_OK;
}
```

## Testing Requirements

### New Test Cases

1. **test_root_pipeline_requires_sources**
   - Verify root pipeline without sources fails validation
   - Check error message is clear and actionable

2. **test_nested_pipeline_allows_no_sources**
   - Verify nested pipelines can be pure transform chains
   - Confirm validation passes when external inputs provided

3. **test_source_detection**
   - Test that various source filter types are correctly identified
   - Verify custom source filters are recognized

### Test Updates Required

| Test File | Test Function | Required Change |
|-----------|--------------|-----------------|
| test_pipeline_integration.c | test_pipeline_dag_data_flow | Include SignalGenerator in pipeline |
| test_pipeline_integration.c | test_pipeline_linear_data_flow | Include SignalGenerator in pipeline |
| test_pipeline_dag.c | All tests | Add mock source filters where needed |

## Rollout Schedule

### Week 1: Preparation
- [ ] Update documentation (pipeline.md, pipeline_property_validation.md)
- [ ] Add migration guide to docs/
- [ ] Implement detection logic with warning mode
- [ ] Create migration script for common patterns

### Week 2: Soft Enforcement
- [ ] Deploy with PIPELINE_SOURCE_WARNING_ONLY flag enabled
- [ ] Monitor warning frequency in CI/CD
- [ ] Update example code and tutorials
- [ ] Assist teams with migration questions

### Week 3: Hard Enforcement
- [ ] Remove PIPELINE_SOURCE_WARNING_ONLY flag
- [ ] Make Bp_EC_INCOMPLETE_PIPELINE a hard error
- [ ] Update all tests to comply
- [ ] Final documentation review

## Backward Compatibility

### Compatibility Flag

During transition, compile with:
```bash
gcc -DPIPELINE_SOURCE_WARNING_ONLY ...
```

This allows gradual migration while maintaining CI/CD stability.

### API Compatibility

No API changes required - only validation behavior changes:
- Same function signatures
- Same structure definitions  
- Only validation logic modified

## Success Criteria

1. **Zero invalid pipelines** in production after migration
2. **All tests pass** with new validation rules
3. **Clear error messages** guide users to correct solutions
4. **No performance impact** from source detection
5. **Documentation** fully reflects new requirements

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Breaking existing code | Phased rollout with warning period |
| Confusion about requirements | Clear documentation and examples |
| Complex migration patterns | Provide migration script/tool |
| Test suite failures | Update tests incrementally during warning phase |

## References

- [Pipeline Documentation](../docs/pipeline.md)
- [Pipeline Property Validation](../docs/pipeline_property_validation.md)
- [Filter Implementation Guide](../docs/filter_implementation_guide.md)

## Appendix: Quick Migration Checklist

- [ ] Identify all Pipeline_t instances in codebase
- [ ] Check each for source filters (n_inputs == 0)
- [ ] For root pipelines without sources:
  - [ ] Option A: Add source filters to pipeline
  - [ ] Option B: Refactor as nested pipeline
  - [ ] Option C: Refactor as filter builder function
- [ ] Update tests to include sources
- [ ] Run validation with warning mode first
- [ ] Fix all warnings before hard enforcement
- [ ] Update any documentation/examples