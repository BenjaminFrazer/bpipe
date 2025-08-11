# Filter Behavioral Compliance Tests

## Overview

This specification defines behavioral compliance tests that validate filters work correctly with inputs conforming to their declared property constraints and produce outputs matching their declared behaviors. Each test verifies both data processing correctness and property contract compliance.

## Test Categories

### 1. Partial Batch Handling Test

**Purpose**: Verify filters correctly handle variable-sized batches according to their declared batch size constraints and behaviors.

```c
typedef struct {
    const char* test_name;
    uint32_t input_batch_sizes[];  // e.g., [64, 32, 1, 64, 17, 64, 3]
    size_t n_batches;
    uint64_t sample_period_ns;     // Consistent across all batches
    
    // Expected output behavior from filter's property contract
    uint32_t min_output_batch;      // From MIN_BATCH_CAPACITY behavior
    uint32_t max_output_batch;      // From MAX_BATCH_CAPACITY behavior
    bool guarantees_full_batches;   // From GUARANTEE_FULL behavior
} PartialBatchTest_t;

void test_partial_batch_handling(void) {
    // Generate sequence with varying batch sizes but consistent timing
    // Batch 1: 64 samples at t=0, period=1000ns
    // Batch 2: 32 samples at t=64000, period=1000ns  
    // Batch 3: 1 sample at t=96000, period=1000ns
    // Batch 4: 17 samples at t=97000, period=1000ns
    
    // Verify INPUT processing:
    // - All samples processed correctly
    // - No samples lost or duplicated
    
    // Verify OUTPUT behaviors match contract:
    // - Output batch sizes within [min_output_batch, max_output_batch]
    // - If guarantees_full_batches, all batches == max_output_batch (except last)
    // - Sample period preserved/transformed per SAMPLE_PERIOD behavior
}
```

### 2. Data Type Compatibility Test

**Purpose**: Verify filters can process all data types they claim to support without errors.

```c
typedef struct {
    const char* test_name;
    SampleDtype_t supported_dtypes[];  // From filter's input constraints
    size_t n_dtypes;
    size_t samples_per_dtype;          // How many samples to test
    uint64_t sample_period_ns;         // Consistent timing for all tests
} DataTypeCompatibilityTest_t;

void test_data_type_compatibility(void) {
    // For each declared supported data type:
    // 1. Create signal generator with that dtype
    // 2. Connect to filter under test
    // 3. Run for N samples
    // 4. Verify no errors (no worker assertions, no NaN/Inf produced)
    // 5. Verify output dtype matches declared behavior
    
    // Example:
    // If filter declares it accepts [DTYPE_FLOAT, DTYPE_I32]:
    // - Test 1: SignalGen(FLOAT) -> Filter -> Verify runs without error
    // - Test 2: SignalGen(I32) -> Filter -> Verify runs without error
}
```

### 3. Sample Rate Preservation Test

**Purpose**: Verify filters maintain or transform sample rates according to their declared SAMPLE_PERIOD_NS behavior.

```c
typedef struct {
    const char* test_name;
    uint64_t input_sample_period_ns;
    size_t total_samples;
    
    // Expected output behavior from filter's property contract
    enum {
        PERIOD_PRESERVE,     // BEHAVIOR_OP_PRESERVE
        PERIOD_SET,         // BEHAVIOR_OP_SET with specific value
        PERIOD_SCALE        // BEHAVIOR_OP_SCALE by factor
    } period_behavior;
    uint64_t expected_output_period_ns;
} SampleRateTest_t;

void test_sample_rate_preservation(void) {
    // Generate 1000 samples at specified input rate
    // Process through filter
    
    // Verify OUTPUT sample period matches declared behavior:
    // - PRESERVE: output_period == input_period
    // - SET: output_period == declared_value
    // - SCALE: output_period == input_period * scale_factor
    
    // Also verify:
    // - No timing drift over extended sequences
    // - Timestamp continuity maintained
}
```

### 4. Data Integrity Test

**Purpose**: Verify filters preserve or transform data correctly according to their type while maintaining declared DATA_TYPE behavior.

```c
typedef struct {
    const char* test_name;
    SampleDtype_t input_dtype;
    DataPattern_t input_pattern;  // RAMP, SINE, IMPULSE, RANDOM
    
    // Expected output behavior from filter's property contract
    SampleDtype_t expected_output_dtype;  // From DATA_TYPE behavior
    TransformFunc_t expected_transform;   // Filter-specific (NULL for passthrough)
} DataIntegrityTest_t;

void test_data_integrity(void) {
    // Generate test pattern with specified input dtype
    // Process through filter
    
    // Verify OUTPUT data type matches declared behavior:
    // - Output dtype == expected_output_dtype
    
    // Verify data transformation:
    // - Passthrough: Bit-exact copy (within dtype)
    // - Map: Function applied correctly to each sample
    // - Cast: Proper type conversion rules applied
    
    // Use multiple patterns to ensure robustness:
    // - Ramp: Detects off-by-one errors
    // - Sine: Detects phase/frequency errors  
    // - Impulse: Detects timing errors
    // - Random: Detects systematic biases
}
```

### 5. Multi-Input Synchronization Test

**Purpose**: Verify filters correctly handle multiple inputs according to their alignment constraints and produce outputs per declared behaviors.

```c
typedef struct {
    const char* test_name;
    size_t n_inputs;
    
    // Input properties
    PropertyTable_t input_properties[MAX_INPUTS];
    int64_t phase_offset_ns[];  // Timing offset per input
    
    // Expected behavior from filter's property contract
    bool requires_alignment;     // CONSTRAINT_OP_MULTI_INPUT_ALIGNED
    PropertyTable_t expected_output_properties;
} MultiInputSyncTest_t;

void test_multi_input_synchronization(void) {
    // Generate synchronized inputs with specified properties
    // Apply phase offsets if testing alignment tolerance
    
    // Verify INPUT handling:
    // - If requires_alignment, properly synchronizes inputs
    // - Processes all inputs correctly
    
    // Verify OUTPUT matches declared behaviors:
    // - Output properties match expected_output_properties
    // - Sample rate preserved/combined per behavior
    // - Data type matches declared output type
    // - Batch sizes follow declared constraints
}
```

## Test Implementation Pattern

Each test follows a consistent pattern using the existing framework:

```c
void run_behavioral_test(FilterRegistration_t* reg, BehavioralTest_t* test) {
    // 1. Setup
    setUp();
    configure_filter_under_test(reg);
    
    // 2. Get filter's declared property contract
    PropertyContract_t* contract = get_filter_contract(g_fut);
    
    // 3. Generate test data conforming to input constraints
    TestData_t* input = generate_conforming_test_data(
        contract->input_constraints, 
        test->pattern_spec
    );
    
    // 4. Create validator for expected output behaviors
    PropertyValidator_t* validator = create_property_validator(
        contract->output_behaviors,
        test->validation_spec
    );
    
    // 5. Connect producer -> filter -> validator
    connect_test_pipeline(producer, g_fut, validator);
    
    // 6. Run test
    start_all_filters();
    wait_for_completion();
    
    // 7. Validate both data correctness AND property compliance
    ValidationResult_t result = validator->get_results();
    TEST_ASSERT_TRUE_MESSAGE(result.data_correct, result.data_failure_reason);
    TEST_ASSERT_TRUE_MESSAGE(result.properties_compliant, result.property_failure_reason);
    
    // 8. Cleanup
    tearDown();
}
```

## Declarative Test Configuration

Tests are configured as data, with expected behaviors derived from filter's property contract:

```c
// Example: Partial batch tests for different filters
PartialBatchTest_t passthrough_partial_tests[] = {
    {
        .test_name = "varying_batch_sizes",
        .input_batch_sizes = {64, 32, 1, 64, 17, 64, 3, 48},
        .n_batches = 8,
        .sample_period_ns = 20833,  // 48kHz
        // Output expectations from filter's declared behaviors:
        .min_output_batch = 1,      // Passthrough: PRESERVE batch size
        .max_output_batch = 64,     // So output matches input range
        .guarantees_full_batches = false
    }
};

PartialBatchTest_t batch_matcher_partial_tests[] = {
    {
        .test_name = "varying_input_full_output",
        .input_batch_sizes = {32, 16, 1, 32, 7, 32, 3, 24},
        .n_batches = 8,
        .sample_period_ns = 20833,
        // Output expectations from filter's declared behaviors:
        .min_output_batch = 128,    // BatchMatcher: SET to fixed size
        .max_output_batch = 128,    
        .guarantees_full_batches = true  // GUARANTEE_FULL behavior
    }
};
```

## Property-Aware Validators

Validators check both data correctness and property contract compliance:

```c
typedef struct {
    // Data validation
    bool (*validate_sample)(size_t idx, float expected, float actual);
    bool (*validate_transform)(Batch_t* input, Batch_t* output, void* context);
    
    // Property contract validation
    bool (*validate_output_dtype)(SampleDtype_t actual, SampleDtype_t expected);
    bool (*validate_sample_period)(uint64_t actual, uint64_t expected, BehaviorOp_t op);
    bool (*validate_batch_size)(size_t actual, size_t min, size_t max, bool full_required);
    
    // Results aggregation
    void (*accumulate_stats)(Batch_t* batch);
    ValidationResult_t (*get_final_result)(void);
} PropertyValidator_t;

// Example validators for different property behaviors
PropertyValidator_t* create_preserve_validator();     // BEHAVIOR_OP_PRESERVE
PropertyValidator_t* create_set_validator();         // BEHAVIOR_OP_SET
PropertyValidator_t* create_scale_validator();       // BEHAVIOR_OP_SCALE
PropertyValidator_t* create_adaptive_validator();    // Complex behaviors
```

## Benefits

1. **Behavioral Focus**: Tests what filters actually do, not just what they claim
2. **Comprehensive Coverage**: Tests various aspects of filter behavior systematically
3. **Reusable**: Same tests apply to all filters of similar types
4. **Realistic**: Tests mirror real-world usage patterns
5. **Diagnostic**: Failures clearly indicate what behavior is incorrect

## Integration with Existing Framework

These tests integrate seamlessly with the existing compliance framework:

```c
// Add to existing test files
void test_filter_behavioral_compliance(void) {
    // Run all behavioral tests for the filter under test
    BehavioralTestSuite_t* suite = get_behavioral_tests(g_reg->type);
    
    for (size_t i = 0; i < suite->n_tests; i++) {
        run_behavioral_test(g_reg, &suite->tests[i]);
    }
}
```

## Implementation Status and Remaining Actions

### Current Implementation Status (as of 2024-08-11)

✅ **Completed:**
- Basic test structure for 4 of 5 test categories
- Integration with existing filter compliance framework
- Basic data flow validation

❌ **Critical Gaps Identified:**
- Multi-Input Synchronization Test not implemented
- Property contract validation missing
- Incorrect error handling patterns
- Missing edge case coverage

### Remaining Implementation Actions

#### Priority 1: Critical Fixes (MUST complete for compliance)

1. **Implement Multi-Input Synchronization Test**
   ```c
   void test_multi_input_synchronization(void) {
       // Skip if filter has < 2 inputs
       if (g_fut->n_input_buffers < 2) {
           TEST_IGNORE_MESSAGE("Filter has fewer than 2 inputs");
           return;
       }
       
       // Get filter's multi-input alignment constraints
       FilterContract_t* contract = &g_fut->contract;
       bool requires_alignment = false;
       for (size_t i = 0; i < contract->n_input_constraints; i++) {
           if (contract->input_constraints[i].op == CONSTRAINT_OP_MULTI_INPUT_ALIGNED) {
               requires_alignment = true;
               break;
           }
       }
       
       // Generate test inputs with phase offsets
       // Verify alignment handling and output properties
   }
   ```

2. **Add Property Contract Validation to All Tests**
   ```c
   // Before each test:
   FilterContract_t* contract = &g_fut->contract;
   
   // Derive expected behaviors from contract
   uint32_t min_batch = get_min_batch_from_behaviors(contract);
   uint32_t max_batch = get_max_batch_from_behaviors(contract);
   
   // Validate outputs match declared behaviors
   validate_output_properties(output, contract->output_behaviors);
   ```

3. **Fix Error Checking Pattern Throughout**
   - Replace all `TEST_ASSERT_EQUAL(Bp_EC_OK, err, msg)` with `CHECK_ERR(function_call)`
   - Add worker error checking after pthread_join:
   ```c
   pthread_join(filter->worker_thread, NULL);
   CHECK_ERR(filter->worker_err_info.ec);
   ```

#### Priority 2: Important Enhancements

1. **Implement Property Validators**
   ```c
   typedef struct {
       PropertyTable_t expected_properties;
       size_t n_batches_validated;
       bool all_properties_match;
       char failure_reason[256];
   } PropertyValidatorState_t;
   
   PropertyValidator_t* create_property_validator(OutputBehavior_t* behaviors, size_t n_behaviors);
   void validate_batch_properties(PropertyValidator_t* v, Batch_t* batch);
   ValidationResult_t get_validation_result(PropertyValidator_t* v);
   ```

2. **Add Comprehensive Edge Case Testing**
   ```c
   // Edge case batch sizes to test
   uint32_t edge_batch_sizes[] = {
       0,    // Empty batch
       1,    // Single sample
       2,    // Minimum non-trivial
       63,   // Just below power of 2
       64,   // Power of 2
       65,   // Just above power of 2
       1024, // Large batch
       0     // Empty batch at end
   };
   ```

3. **Add Batch Metadata Validation**
   ```c
   // Verify timestamp continuity
   uint64_t expected_t_ns = last_t_ns + (last_batch_size * period_ns);
   TEST_ASSERT_EQUAL_MESSAGE(expected_t_ns, batch->t_ns, 
                            "Timestamp discontinuity detected");
   
   // Verify period consistency
   TEST_ASSERT_EQUAL_MESSAGE(expected_period_ns, batch->period_ns,
                            "Sample period changed unexpectedly");
   ```

#### Priority 3: Quality Improvements

1. **Make Tests Data-Driven**
   ```c
   // Define test suites per filter type
   BehavioralTestSuite_t passthrough_suite = {
       .partial_batch_tests = passthrough_partial_tests,
       .n_partial_tests = ARRAY_SIZE(passthrough_partial_tests),
       .dtype_tests = passthrough_dtype_tests,
       .n_dtype_tests = ARRAY_SIZE(passthrough_dtype_tests),
       // ... etc
   };
   ```

2. **Add Transform Validation**
   ```c
   // For MAP filters, verify transform is applied
   if (filter->filt_type == FILT_T_MAP) {
       MapConfig_t* config = (MapConfig_t*)filter->config;
       for (size_t i = 0; i < batch->head; i++) {
           float expected = config->map_fcn(input->data[i]);
           TEST_ASSERT_FLOAT_WITHIN(EPSILON, expected, output->data[i]);
       }
   }
   ```

3. **Add Timeout Protection**
   ```c
   // Use timeout wrapper for all blocking operations
   #define TEST_TIMEOUT_US 5000000  // 5 seconds
   
   // Replace usleep with timeout-protected wait
   wait_for_completion_with_timeout(TEST_TIMEOUT_US);
   ```

### Testing Philosophy Compliance

All remaining implementations MUST follow these principles from `docs/testing_guidelines.md`:

1. **Use CHECK_ERR macro**: Consistent error checking in tests
2. **Test edge cases**: Empty batches, max sizes, boundary conditions  
3. **Use timeout wrapper**: Prevent hanging tests
4. **Check worker errors**: Always verify worker_err_info.ec
5. **Document test intent**: Each test should have a description comment

### Validation Checklist

Before considering implementation complete, verify:

- [ ] All 5 test categories implemented
- [ ] Property contract validation integrated
- [ ] CHECK_ERR macro used throughout
- [ ] Edge cases covered (empty, single, max)
- [ ] Worker errors properly checked
- [ ] Batch metadata validated (t_ns, period_ns)
- [ ] Multi-input alignment tested
- [ ] Transform correctness verified
- [ ] Tests are data-driven
- [ ] Timeout protection added
- [ ] Test intent documented

### Expected Outcomes

When complete, these tests should:
1. Catch filters that don't honor their declared property contracts
2. Detect data loss, corruption, or duplication
3. Verify timing preservation/transformation
4. Validate multi-input synchronization
5. Ensure robust edge case handling
6. Provide clear diagnostic messages on failure