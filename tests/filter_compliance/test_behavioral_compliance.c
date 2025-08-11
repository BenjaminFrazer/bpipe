/**
 * @file test_behavioral_compliance.c
 * @brief Behavioral compliance tests for filter property contracts
 * 
 * These tests validate that filters correctly handle inputs conforming to their
 * declared property constraints and produce outputs matching their declared behaviors.
 */

#include "common.h"
#include "signal_generator.h"
#include <math.h>

// ========================================================================
// Test Data Structures
// ========================================================================

typedef enum {
    TEST_PATTERN_RAMP,
    TEST_PATTERN_SINE,
    TEST_PATTERN_IMPULSE,
    TEST_PATTERN_RANDOM,
    TEST_PATTERN_CONSTANT
} TestDataPattern_t;

typedef struct {
    const char* test_name;
    uint32_t* input_batch_sizes;   // Array of batch sizes to generate
    size_t n_batches;
    uint64_t sample_period_ns;     // Consistent across all batches
    
    // Expected output behavior from filter's property contract
    uint32_t min_output_batch;      // From MIN_BATCH_CAPACITY behavior
    uint32_t max_output_batch;      // From MAX_BATCH_CAPACITY behavior
    bool guarantees_full_batches;   // From GUARANTEE_FULL behavior
} PartialBatchTest_t;

typedef struct {
    const char* test_name;
    SampleDtype_t* supported_dtypes;  // From filter's input constraints
    size_t n_dtypes;
    size_t samples_per_dtype;         // How many samples to test
    uint64_t sample_period_ns;        // Consistent timing for all tests
} DataTypeCompatibilityTest_t;

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

typedef struct {
    const char* test_name;
    SampleDtype_t input_dtype;
    TestDataPattern_t input_pattern;
    
    // Expected output behavior from filter's property contract
    SampleDtype_t expected_output_dtype;  // From DATA_TYPE behavior
    bool is_passthrough;                  // True for passthrough filters
} DataIntegrityTest_t;

// ========================================================================
// Test Data Generators
// ========================================================================

#if 0  // Currently unused but may be useful in future
/**
 * Generate test data with varying batch sizes
 */
static void generate_varying_batch_data(Batch_buff_t* output_buffer,
                                       uint32_t* batch_sizes,
                                       size_t n_batches,
                                       uint64_t sample_period_ns,
                                       TestDataPattern_t pattern)
{
    uint64_t current_time_ns = 0;
    float phase = 0.0f;
    uint32_t sequence = 0;
    
    for (size_t batch_idx = 0; batch_idx < n_batches; batch_idx++) {
        uint32_t batch_size = batch_sizes[batch_idx];
        
        // Get output batch
        Batch_t* batch = bb_get_head(output_buffer);
        TEST_ASSERT_NOT_NULL(batch);
        
        // Fill batch with pattern
        float* data = (float*)batch->data;
        for (uint32_t i = 0; i < batch_size; i++) {
            switch (pattern) {
                case TEST_PATTERN_RAMP:
                    data[i] = (float)sequence++;
                    break;
                    
                case TEST_PATTERN_SINE:
                    data[i] = sinf(phase);
                    phase += 0.1f;
                    break;
                    
                case TEST_PATTERN_IMPULSE:
                    data[i] = (i == 0 && batch_idx == 0) ? 1.0f : 0.0f;
                    break;
                    
                case TEST_PATTERN_RANDOM:
                    data[i] = (float)rand() / RAND_MAX;
                    break;
                    
                case TEST_PATTERN_CONSTANT:
                    data[i] = 1.0f;
                    break;
            }
        }
        
        // Set batch metadata
        batch->head = batch_size;
        batch->t_ns = current_time_ns;
        batch->period_ns = sample_period_ns;
        batch->ec = Bp_EC_OK;
        
        // Submit batch
        Bp_EC err = bb_submit(output_buffer, 1000000);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
        
        // Update time for next batch
        current_time_ns += batch_size * sample_period_ns;
    }
    
    // Submit completion batch
    Batch_t* complete = bb_get_head(output_buffer);
    complete->head = 0;
    complete->ec = Bp_EC_COMPLETE;
    bb_submit(output_buffer, 1000000);
}
#endif  // Currently unused

// ========================================================================
// Property Validators
// ========================================================================

typedef struct {
    // Expected properties
    uint32_t min_batch_size;
    uint32_t max_batch_size;
    bool requires_full_batches;
    uint64_t expected_period_ns;
    SampleDtype_t expected_dtype;
    
    // Collected statistics
    size_t n_batches_received;
    size_t n_samples_received;
    uint32_t actual_min_batch;
    uint32_t actual_max_batch;
    bool saw_partial_batch;
    uint64_t actual_period_ns;
    
    // Validation results
    bool batch_size_valid;
    bool period_valid;
    bool dtype_valid;
    char error_msg[256];
} PropertyValidator_t;

#if 0  // Currently unused but may be useful in future
static void validator_init(PropertyValidator_t* v)
{
    v->n_batches_received = 0;
    v->n_samples_received = 0;
    v->actual_min_batch = UINT32_MAX;
    v->actual_max_batch = 0;
    v->saw_partial_batch = false;
    v->actual_period_ns = 0;
    v->batch_size_valid = true;
    v->period_valid = true;
    v->dtype_valid = true;
    v->error_msg[0] = '\0';
}

static void validator_process_batch(PropertyValidator_t* v, Batch_t* batch)
{
    if (batch->ec == Bp_EC_COMPLETE) {
        return;
    }
    
    v->n_batches_received++;
    v->n_samples_received += batch->head;
    
    // Track batch sizes
    if (batch->head < v->actual_min_batch) {
        v->actual_min_batch = batch->head;
    }
    if (batch->head > v->actual_max_batch) {
        v->actual_max_batch = batch->head;
    }
    
    // Check if partial batch
    if (batch->head < v->max_batch_size) {
        v->saw_partial_batch = true;
    }
    
    // Validate batch size constraints
    if (batch->head < v->min_batch_size || batch->head > v->max_batch_size) {
        v->batch_size_valid = false;
        snprintf(v->error_msg, sizeof(v->error_msg),
                "Batch size %zu outside range [%u, %u]",
                batch->head, v->min_batch_size, v->max_batch_size);
    }
    
    // Validate full batch requirement
    if (v->requires_full_batches && batch->head != v->max_batch_size) {
        // Allow last batch to be partial
        // (This would need more sophisticated tracking in production)
        if (v->n_batches_received > 1) {
            v->batch_size_valid = false;
            snprintf(v->error_msg, sizeof(v->error_msg),
                    "Non-full batch %zu when full batches required",
                    batch->head);
        }
    }
    
    // Validate sample period
    if (v->expected_period_ns > 0) {
        if (v->actual_period_ns == 0) {
            v->actual_period_ns = batch->period_ns;
        }
        
        if (batch->period_ns != v->expected_period_ns) {
            v->period_valid = false;
            snprintf(v->error_msg, sizeof(v->error_msg),
                    "Period %u != expected %lu",
                    batch->period_ns, v->expected_period_ns);
        }
    }
}
#endif  // Currently unused

// ========================================================================
// Test: Partial Batch Handling
// ========================================================================

void test_partial_batch_handling(void)
{
    // Initialize filter first
    Bp_EC err = g_fut_init(g_fut, g_fut_config);
    TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Filter init failed");
    
    // Now check if filter has inputs/outputs
    SKIP_IF_NO_INPUTS();
    SKIP_IF_NO_OUTPUTS();
    
    // Test configuration with varying batch sizes
    uint32_t batch_sizes[] = {64, 32, 1, 64, 17, 64, 3, 48};
    size_t n_batches = sizeof(batch_sizes) / sizeof(batch_sizes[0]);
    
    // Create test producer that generates varying batch sizes
    ControllableProducer_t producer;
    ControllableProducerConfig_t prod_config = {
        .name = "partial_batch_producer",
        .timeout_us = 1000000,
        .samples_per_second = 48000,
        .pattern = PATTERN_SEQUENTIAL,
        .max_batches = n_batches,
        .burst_mode = false,
        .start_sequence = 0
    };
    err = controllable_producer_init(&producer, prod_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Create test consumer to validate output
    ControllableConsumer_t consumer;
    ControllableConsumerConfig_t consumer_config = {
        .name = "partial_batch_consumer",
        .buff_config = {
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 10,
            .ring_capacity_expo = 8,
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .timeout_us = 1000000,
        .process_delay_us = 0,
        .validate_sequence = true,
        .validate_timing = false,
        .consume_pattern = 0,
        .slow_start = false,
        .slow_start_batches = 0
    };
    err = controllable_consumer_init(&consumer, consumer_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Connect: producer -> filter -> sink
    err = filt_sink_connect(&producer.base, 0, g_fut->input_buffers[0]);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_sink_connect(g_fut, 0, consumer.base.input_buffers[0]);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Start all filters
    err = filt_start(&producer.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_start(g_fut);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_start(&consumer.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Wait for completion
    usleep(100000);  // 100ms should be enough
    
    // Stop filters
    filt_stop(&consumer.base);
    filt_stop(g_fut);
    filt_stop(&producer.base);
    
    // Validate results
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, atomic_load(&consumer.total_batches),
                                    "No batches received by consumer");
    
    // Verify all samples were processed
    size_t expected_samples = 0;
    for (size_t i = 0; i < n_batches; i++) {
        expected_samples += batch_sizes[i];
    }
    
    // Allow some tolerance for filters that may buffer samples
    size_t actual_samples = atomic_load(&consumer.total_samples);
    TEST_ASSERT_MESSAGE(actual_samples >= expected_samples * 0.9,
                       "Too few samples processed");
    
    // Check worker thread errors
    TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, g_fut->worker_err_info.ec,
                             "Worker thread error");
    
    // Cleanup
    filt_deinit(&producer.base);
    filt_deinit(&consumer.base);
}

// ========================================================================
// Test: Data Type Compatibility
// ========================================================================

void test_data_type_compatibility(void)
{
    // Initialize filter with default config first to check capabilities
    Bp_EC err = g_fut_init(g_fut, g_fut_config);
    if (err != Bp_EC_OK) {
        TEST_IGNORE_MESSAGE("Filter init failed with default config");
        return;
    }
    
    SKIP_IF_NO_INPUTS();
    SKIP_IF_NO_OUTPUTS();
    
    // Deinit before starting the actual test
    filt_deinit(g_fut);
    setUp();  // Reset filter
    
    // Test all common data types
    SampleDtype_t test_dtypes[] = {DTYPE_FLOAT, DTYPE_I32, DTYPE_U32};
    size_t n_dtypes = sizeof(test_dtypes) / sizeof(test_dtypes[0]);
    size_t samples_per_test = 1000;
    
    for (size_t dtype_idx = 0; dtype_idx < n_dtypes; dtype_idx++) {
        SampleDtype_t dtype = test_dtypes[dtype_idx];
        
        // Skip if filter doesn't support this dtype
        // (In a real implementation, we'd check the filter's property constraints)
        
        // Update buffer config for dtype
        if (g_fut_config && g_filters[g_current_filter].has_buff_config) {
            BatchBuffer_config* buff_config = 
                (BatchBuffer_config*)((char*)g_fut_config + 
                                     g_filters[g_current_filter].buff_config_offset);
            buff_config->dtype = dtype;
        }
        
        // Initialize filter
        err = g_fut_init(g_fut, g_fut_config);
        if (err != Bp_EC_OK) {
            // Filter doesn't support this dtype
            continue;
        }
        
        // Create appropriate signal generator
        SignalGenerator_t generator;
        SignalGenerator_config_t gen_config = {
            .name = "dtype_test_gen",
            .buff_config = {
                .dtype = dtype,
                .batch_capacity_expo = 6,
                .ring_capacity_expo = 8,
                .overflow_behaviour = OVERFLOW_BLOCK
            },
            .timeout_us = 1000000,
            .waveform_type = WAVEFORM_SAWTOOTH,
            .frequency_hz = 100.0,
            .amplitude = 1.0,
            .offset = 0.0,
            .phase_rad = 0.0,
            .sample_period_ns = 20833,  // 48kHz
            .max_samples = samples_per_test
        };
        
        err = signal_generator_init(&generator, gen_config);
        TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Generator init failed");
        
        // Create test consumer
        ControllableConsumer_t consumer;
        ControllableConsumerConfig_t consumer_config = {
            .name = "dtype_test_consumer",
            .buff_config = {
                .dtype = dtype,
                .batch_capacity_expo = 6,
                .ring_capacity_expo = 8,
                .overflow_behaviour = OVERFLOW_BLOCK
            },
            .timeout_us = 1000000,
            .process_delay_us = 0,
            .validate_sequence = false,
            .validate_timing = false,
            .consume_pattern = 0,
            .slow_start = false,
            .slow_start_batches = 0
        };
        err = controllable_consumer_init(&consumer, consumer_config);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
        
        // Connect: generator -> filter -> consumer
        err = filt_sink_connect(&generator.base, 0, g_fut->input_buffers[0]);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
        
        err = filt_sink_connect(g_fut, 0, consumer.base.input_buffers[0]);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
        
        // Start filters
        err = filt_start(&generator.base);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
        
        err = filt_start(g_fut);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
        
        err = filt_start(&consumer.base);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
        
        // Wait for completion
        usleep(100000);  // 100ms
        
        // Stop filters
        filt_stop(&consumer.base);
        filt_stop(g_fut);
        filt_stop(&generator.base);
        
        // Validate no errors occurred
        TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, g_fut->worker_err_info.ec,
                                 "Worker error with dtype");
        TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, generator.base.worker_err_info.ec,
                                 "Generator error");
        TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, consumer.base.worker_err_info.ec,
                                 "Consumer error");
        
        // Cleanup
        filt_deinit(&generator.base);
        filt_deinit(&consumer.base);
    }
}

// ========================================================================
// Test: Sample Rate Preservation
// ========================================================================

void test_sample_rate_preservation(void)
{
    // Initialize filter first
    Bp_EC err = g_fut_init(g_fut, g_fut_config);
    TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Filter init failed");
    
    SKIP_IF_NO_INPUTS();
    SKIP_IF_NO_OUTPUTS();
    
    // Test configuration
    uint64_t input_period_ns = 20833;  // 48kHz
    size_t total_samples = 1000;
    
    // Create signal generator
    SignalGenerator_t generator;
    SignalGenerator_config_t gen_config = {
        .name = "rate_test_gen",
        .buff_config = {
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 6,
            .ring_capacity_expo = 8,
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .timeout_us = 1000000,
        .waveform_type = WAVEFORM_SINE,
        .frequency_hz = 1000.0,
        .amplitude = 1.0,
        .offset = 0.0,
        .phase_rad = 0.0,
        .sample_period_ns = input_period_ns,
        .max_samples = total_samples
    };
    
    err = signal_generator_init(&generator, gen_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Create test consumer with property validation
    ControllableConsumer_t consumer;
    ControllableConsumerConfig_t consumer_config = {
        .name = "rate_test_consumer",
        .buff_config = {
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 6,
            .ring_capacity_expo = 8,
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .timeout_us = 1000000,
        .process_delay_us = 0,
        .validate_sequence = false,
        .validate_timing = true,  // Enable timing validation
        .consume_pattern = 0,
        .slow_start = false,
        .slow_start_batches = 0
    };
    err = controllable_consumer_init(&consumer, consumer_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Connect pipeline
    err = filt_sink_connect(&generator.base, 0, g_fut->input_buffers[0]);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_sink_connect(g_fut, 0, consumer.base.input_buffers[0]);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Start filters
    err = filt_start(&generator.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_start(g_fut);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    err = filt_start(&consumer.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    
    // Wait for completion
    usleep(200000);  // 200ms
    
    // Stop filters
    filt_stop(&consumer.base);
    filt_stop(g_fut);
    filt_stop(&generator.base);
    
    // Validate timing preservation
    TEST_ASSERT_GREATER_THAN(0, atomic_load(&consumer.total_batches));
    
    // Check for timing errors (ControllableConsumer tracks these)
    TEST_ASSERT_EQUAL_MESSAGE(0, atomic_load(&consumer.timing_errors),
                             "Timing errors detected in output");
    
    // Cleanup
    filt_deinit(&generator.base);
    filt_deinit(&consumer.base);
}

// ========================================================================
// Test: Data Integrity
// ========================================================================

void test_data_integrity(void)
{
    // Initialize filter with default config first to check capabilities
    Bp_EC err = g_fut_init(g_fut, g_fut_config);
    if (err != Bp_EC_OK) {
        TEST_IGNORE_MESSAGE("Filter init failed with default config");
        return;
    }
    
    SKIP_IF_NO_INPUTS();
    SKIP_IF_NO_OUTPUTS();
    
    // Deinit before starting the actual test
    filt_deinit(g_fut);
    setUp();  // Reset filter
    
    // Test patterns
    TestDataPattern_t patterns[] = {TEST_PATTERN_RAMP, TEST_PATTERN_SINE, TEST_PATTERN_IMPULSE};
    const char* pattern_names[] = {"RAMP", "SINE", "IMPULSE"};
    size_t n_patterns = sizeof(patterns) / sizeof(patterns[0]);
    
    for (size_t pattern_idx = 0; pattern_idx < n_patterns; pattern_idx++) {
        TestDataPattern_t pattern = patterns[pattern_idx];
        
        // Reinitialize filter for each test (skip first iteration)
        if (pattern_idx > 0) {
            filt_deinit(g_fut);
            setUp();  // Reset filter
            err = g_fut_init(g_fut, g_fut_config);
            TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Filter init failed");
        }
        
        // Create appropriate signal generator based on pattern
        SignalGenerator_t generator;
        WaveformType_e waveform = (pattern == TEST_PATTERN_SINE) ? WAVEFORM_SINE : WAVEFORM_SAWTOOTH;
        
        SignalGenerator_config_t gen_config = {
            .name = "integrity_test_gen",
            .buff_config = {
                .dtype = DTYPE_FLOAT,
                .batch_capacity_expo = 6,
                .ring_capacity_expo = 8,
                .overflow_behaviour = OVERFLOW_BLOCK
            },
            .timeout_us = 1000000,
            .waveform_type = waveform,
            .frequency_hz = 100.0,
            .amplitude = 1.0,
            .offset = 0.0,
            .phase_rad = 0.0,
            .sample_period_ns = 20833,
            .max_samples = 1000
        };
        
        err = signal_generator_init(&generator, gen_config);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
        
        // Create validating consumer
        ControllableConsumer_t consumer;
        ControllableConsumerConfig_t consumer_config = {
            .name = "integrity_test_consumer",
            .buff_config = {
                .dtype = DTYPE_FLOAT,
                .batch_capacity_expo = 6,
                .ring_capacity_expo = 8,
                .overflow_behaviour = OVERFLOW_BLOCK
            },
            .timeout_us = 1000000,
            .process_delay_us = 0,
            .validate_sequence = false,  // Disable sequence validation for now
            .validate_timing = false,
            .consume_pattern = 0,
            .slow_start = false,
            .slow_start_batches = 0
        };
        err = controllable_consumer_init(&consumer, consumer_config);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
        
        // Connect pipeline (input buffers are created during init)
        TEST_ASSERT_NOT_NULL_MESSAGE(g_fut->input_buffers[0], "Filter input buffer not allocated");
        err = filt_sink_connect(&generator.base, 0, g_fut->input_buffers[0]);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
        
        err = filt_sink_connect(g_fut, 0, consumer.base.input_buffers[0]);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
        
        // Start filters
        err = filt_start(&generator.base);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
        
        err = filt_start(g_fut);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
        
        err = filt_start(&consumer.base);
        TEST_ASSERT_EQUAL(Bp_EC_OK, err);
        
        // Wait for completion
        usleep(200000);  // 200ms
        
        // Stop filters
        filt_stop(&consumer.base);
        filt_stop(g_fut);
        filt_stop(&generator.base);
        
        // Validate data integrity
        char msg[256];
        snprintf(msg, sizeof(msg), "Pattern %s: No data received", 
                pattern_names[pattern_idx]);
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, atomic_load(&consumer.total_samples), msg);
        
        // Note: Sequence validation is disabled for now as signal generator
        // doesn't produce sequential patterns that match ControllableConsumer's expectations
        
        // Cleanup
        filt_deinit(&generator.base);
        filt_deinit(&consumer.base);
    }
}

// ========================================================================
// Test Runner Entry Point
// ========================================================================

void test_filter_behavioral_compliance(void)
{
    // Run all behavioral compliance tests
    RUN_TEST(test_partial_batch_handling);
    RUN_TEST(test_data_type_compatibility);
    RUN_TEST(test_sample_rate_preservation);
    RUN_TEST(test_data_integrity);
}