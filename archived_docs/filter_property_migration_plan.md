# Filter Property System Migration Plan

## Overview
This document outlines the migration plan for updating all existing filters to use the property system for input validation and output capability declaration.

## New API Design
To simplify filter migration, we will add a helper function that handles both input constraints and output behaviors for common filter patterns:

```c
void prop_set_output_behavior_for_buffer_filter(Filter_t* filter,
                                                const BatchBuffer_config* config,
                                                bool adapt_batch_size,
                                                bool guarantee_full);
```

### Parameters:
- `adapt_batch_size`: false = passthrough input sizes (clamped to buffer), true = filter determines output size
- `guarantee_full`: false = allows partial batches, true = always outputs full batches (min==max)

## Current State
- **Migrated (3/10)**: signal_generator, csv_sink, map
- **Pending (7+)**: passthrough, tee, debug_output_filter, csv_source, batch_matcher, sample_aligner, batch_buffer

## Migration Priority

### Phase 1: Simple Pass-Through Filters (Quick Wins)
These filters preserve most properties but must handle buffer size constraints.

#### 1.1 Passthrough Filter
- **File**: `bpipe/passthrough.c`
- **Pattern**: Simple Pass-Through (Pattern 1)
- **Migration**: 
  ```c
  prop_constraints_from_buffer_append(&pt->base, &config.buff_config, true);
  prop_set_output_behavior_for_buffer_filter(&pt->base, &config.buff_config,
                                            false,  // passthrough
                                            false); // allows partial
  ```
- **Effort**: 5 minutes

#### 1.2 Tee Filter
- **File**: `bpipe/tee.c`
- **Pattern**: Simple Pass-Through (Pattern 1)
- **Migration**:
  ```c
  prop_constraints_from_buffer_append(&tee->base, &config.buff_config, true);
  prop_set_output_behavior_for_buffer_filter(&tee->base, &config.buff_config,
                                            false,  // passthrough
                                            false); // allows partial
  ```
- **Effort**: 5 minutes

#### 1.3 Debug Output Filter
- **File**: `bpipe/debug_output_filter.c`
- **Pattern**: Simple Pass-Through (Pattern 1)
- **Migration**:
  ```c
  prop_constraints_from_buffer_append(&dbg->base, &config.buff_config, true);
  prop_set_output_behavior_for_buffer_filter(&dbg->base, &config.buff_config,
                                            false,  // passthrough
                                            false); // allows partial
  ```
- **Effort**: 5 minutes

### Phase 2: Source Filters
These filters generate data and must declare output properties.

#### 2.1 CSV Source
- **File**: `bpipe/csv_source.c`
- **Pattern**: Source Filter (Pattern 3)
- **Migration**:
  ```c
  prop_set_dtype(&src->base.output_properties, DTYPE_FLOAT);
  // Only set if regular timing detected
  if (src->is_regular && src->detected_period_ns > 0) {
    prop_set_sample_period(&src->base.output_properties, src->detected_period_ns);
  }
  // Adaptive batching
  prop_set_min_batch_capacity(&src->base.output_properties, 1);
  prop_set_max_batch_capacity(&src->base.output_properties, UINT32_MAX);
  ```
- **Effort**: 15 minutes

### Phase 3: Complex Processing Filters
These filters have specific requirements for synchronization or transformation.

#### 3.1 Batch Matcher
- **File**: `bpipe/batch_matcher.c`
- **Pattern**: Adaptive Batch Size Filter (Pattern 2)
- **Migration**:
  ```c
  prop_constraints_from_buffer_append(&bm->base, &config.buff_config, true);
  prop_append_constraint(&bm->base, PROP_SAMPLE_PERIOD_NS, 
                        CONSTRAINT_OP_EXISTS, NULL);
  prop_set_output_behavior_for_buffer_filter(&bm->base, &config.buff_config,
                                            true,   // adapt
                                            true);  // full batches
  // Later in start_impl when sink detected:
  // prop_append_behavior(&bm->base, PROP_MIN/MAX_BATCH_CAPACITY, ...)
  ```
- **Effort**: 15 minutes

#### 3.2 Sample Aligner
- **File**: `bpipe/sample_aligner.c`
- **Pattern**: Adaptive or Pass-Through depending on config
- **Migration**:
  ```c
  prop_constraints_from_buffer_append(&sa->base, &config.buff_config, true);
  prop_append_constraint(&sa->base, PROP_SAMPLE_PERIOD_NS,
                        CONSTRAINT_OP_EXISTS, NULL);
  // If resampling:
  prop_set_output_behavior_for_buffer_filter(&sa->base, &config.buff_config,
                                            true,   // adapt
                                            false); // may be partial
  // If just aligning:
  prop_set_output_behavior_for_buffer_filter(&sa->base, &config.buff_config,
                                            false,  // passthrough
                                            false); // may be partial
  ```
- **Effort**: 15 minutes

#### 3.3 Batch Buffer (if needed)
- **File**: `bpipe/batch_buffer.c`
- **Note**: May not need migration if only used internally
- **Effort**: 10 minutes (if required)

## Implementation Patterns

### Pattern 1: Simple Pass-Through Filter
Filters that pass data through with minimal processing (passthrough, tee, debug_output).

```c
// In filter_init() after filt_init():

// Set input constraints based on buffer capacity
prop_constraints_from_buffer_append(&filter->base, &config.buff_config, 
                                   true);  // accepts partial batches

// Set output behaviors - passthrough mode, allows partial batches
prop_set_output_behavior_for_buffer_filter(&filter->base, &config.buff_config,
                                          false,  // passthrough (not adapt)
                                          false); // allows partial batches
```

### Pattern 2: Adaptive Batch Size Filter
Filters that change batch sizes based on downstream requirements (batch_matcher, sample_aligner).

```c
// In filter_init() after filt_init():

// Set input constraints
prop_constraints_from_buffer_append(&filter->base, &config.buff_config, 
                                   true);  // accepts partial batches

// Set output behaviors - adaptive mode with full batches
prop_set_output_behavior_for_buffer_filter(&filter->base, &config.buff_config,
                                          true,   // adapt batch size
                                          true);  // guarantee full batches

// Later, when sink requirements are detected:
uint32_t detected_size = /* detected from sink */;
prop_append_behavior(&filter->base, PROP_MIN_BATCH_CAPACITY, 
                    BEHAVIOR_OP_SET, &detected_size);
prop_append_behavior(&filter->base, PROP_MAX_BATCH_CAPACITY, 
                    BEHAVIOR_OP_SET, &detected_size);
```

### Pattern 3: Source Filter (No Inputs)
Source filters must explicitly set all output properties since they have no inputs.

```c
// In filter_init() after filt_init():

// Set all output properties explicitly
prop_set_dtype(&filter->base.output_properties, DTYPE_FLOAT);

// Set sample period if known
if (filter->is_regular && filter->detected_period_ns > 0) {
  prop_set_sample_period(&filter->base.output_properties, filter->detected_period_ns);
}

// Set batch capacity based on how the source generates data
prop_set_min_batch_capacity(&filter->base.output_properties, min_size);
prop_set_max_batch_capacity(&filter->base.output_properties, max_size);
```

### Pattern 4: Sink Filter (No Outputs)
Sink filters only declare input constraints, no output behaviors.

```c
// In filter_init() after filt_init():

// Declare what inputs we can accept
prop_append_constraint(&filter->base, PROP_DATA_TYPE, CONSTRAINT_OP_EQ, 
                      &(SampleDtype_t){DTYPE_FLOAT});
prop_append_constraint(&filter->base, PROP_SAMPLE_PERIOD_NS, 
                      CONSTRAINT_OP_EXISTS, NULL);

// If using buffer-based constraints:
prop_constraints_from_buffer_append(&filter->base, &config.buff_config, true);
```

### Pattern 5: Processing Filter with Full Batch Requirement
Filters that require full batches for algorithms (FFT, block processing).

```c
// In filter_init() after filt_init():

// Require full input batches
prop_constraints_from_buffer_append(&filter->base, &config.buff_config, 
                                   false); // requires full batches

// Output full batches at buffer capacity
prop_set_output_behavior_for_buffer_filter(&filter->base, &config.buff_config,
                                          false,  // passthrough size
                                          true);  // guarantee full batches
```

## Testing Strategy

### Phase 1 Testing
- Run existing unit tests for each filter
- Verify no regression in functionality
- Add simple property verification test

### Phase 2 Testing  
- Create connection validation tests
- Test incompatible connections are rejected
- Verify error messages are clear

### Phase 3: Compliance Suite Update
- Add property validation tests to compliance suite
- Test cross-filter compatibility matrix
- Verify multi-input synchronization constraints

## Success Criteria

1. **All filters declare properties**: 100% migration complete
2. **No regressions**: All existing tests pass
3. **Connection validation works**: Incompatible connections rejected at init
4. **Clear error messages**: Users understand why connections fail
5. **Compliance tests updated**: Property system fully tested

## Timeline Estimate

- **Phase 1**: 15 minutes (3 simple filters using new API)
- **Phase 2**: 15 minutes (1 adaptive source filter)  
- **Phase 3**: 30 minutes (2 processing filters)
- **Testing**: 30 minutes
- **Total**: ~1.5 hours

## Rollback Plan

If issues arise:
1. Properties are additive - old code still works
2. Can disable validation temporarily with flag
3. Each filter can be migrated independently
4. Git history allows easy revert per filter

## Next Steps

1. Start with Phase 1 simple filters (passthrough, tee, debug_output)
2. Run tests after each migration
3. Move to Phase 2/3 once Phase 1 stable
4. Update compliance tests after all filters migrated