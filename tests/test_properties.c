#include "unity.h"
#include "properties.h"
#include "signal_generator.h"
#include "csv_sink.h"
#include "map.h"
#include "utils.h"
#include <stdio.h>

// CHECK_ERR macro for error handling
#define CHECK_ERR(ERR)                                          \
  do {                                                          \
    Bp_EC _ec = ERR;                                            \
    TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, _ec, err_lut[_ec]); \
  } while (false);

void setUp(void) {}
void tearDown(void) {}

void test_property_table_init(void) {
    PropertyTable_t table = prop_table_init();
    
    // All properties should be unknown initially
    for (int i = 0; i < PROP_COUNT_MVP; i++) {
        TEST_ASSERT_FALSE(table.properties[i].known);
    }
}

void test_property_setters_getters(void) {
    PropertyTable_t table = prop_table_init();
    
    // Test data type property
    CHECK_ERR(prop_set_dtype(&table, DTYPE_FLOAT));
    SampleDtype_t dtype;
    TEST_ASSERT_TRUE(prop_get_dtype(&table, &dtype));
    TEST_ASSERT_EQUAL(DTYPE_FLOAT, dtype);
    
    // Test batch capacity properties
    CHECK_ERR(prop_set_min_batch_capacity(&table, 64));
    CHECK_ERR(prop_set_max_batch_capacity(&table, 1024));
    uint32_t min_cap, max_cap;
    TEST_ASSERT_TRUE(prop_get_min_batch_capacity(&table, &min_cap));
    TEST_ASSERT_TRUE(prop_get_max_batch_capacity(&table, &max_cap));
    TEST_ASSERT_EQUAL(64, min_cap);
    TEST_ASSERT_EQUAL(1024, max_cap);
    
    // Test sample rate property
    CHECK_ERR(prop_set_sample_rate_hz(&table, 48000));
    uint32_t rate;
    TEST_ASSERT_TRUE(prop_get_sample_rate_hz(&table, &rate));
    TEST_ASSERT_EQUAL(48000, rate);
}

void test_constraint_validation_exists(void) {
    PropertyTable_t table = prop_table_init();
    
    // Constraint requires sample rate to exist
    InputConstraint_t constraints[] = {
        { PROP_SAMPLE_PERIOD_NS, CONSTRAINT_OP_EXISTS, {0} }
    };
    
    FilterContract_t contract = {
        .input_constraints = constraints,
        .n_input_constraints = 1,
        .output_behaviors = NULL,
        .n_output_behaviors = 0
    };
    
    char error_msg[256];
    
    // Should fail when property is unknown
    Bp_EC err = prop_validate_connection(&table, &contract, error_msg, sizeof(error_msg));
    TEST_ASSERT_EQUAL(Bp_EC_PROPERTY_MISMATCH, err);
    
    // Should pass when property is set
    prop_set_sample_rate_hz(&table, 48000);
    err = prop_validate_connection(&table, &contract, error_msg, sizeof(error_msg));
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
}

void test_constraint_validation_equality(void) {
    PropertyTable_t table = prop_table_init();
    prop_set_dtype(&table, DTYPE_FLOAT);
    
    // Constraint requires DTYPE_I32
    InputConstraint_t constraints[] = {
        { PROP_DATA_TYPE, CONSTRAINT_OP_EQ, {.dtype = DTYPE_I32} }
    };
    
    FilterContract_t contract = {
        .input_constraints = constraints,
        .n_input_constraints = 1,
        .output_behaviors = NULL,
        .n_output_behaviors = 0
    };
    
    char error_msg[256];
    
    // Should fail - type mismatch
    Bp_EC err = prop_validate_connection(&table, &contract, error_msg, sizeof(error_msg));
    TEST_ASSERT_EQUAL(Bp_EC_PROPERTY_MISMATCH, err);
    
    // Should pass when types match
    prop_set_dtype(&table, DTYPE_I32);
    err = prop_validate_connection(&table, &contract, error_msg, sizeof(error_msg));
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
}

void test_constraint_validation_range(void) {
    PropertyTable_t table = prop_table_init();
    prop_set_min_batch_capacity(&table, 64);
    
    // Constraint requires min batch capacity >= 128
    InputConstraint_t constraints[] = {
        { PROP_MIN_BATCH_CAPACITY, CONSTRAINT_OP_GTE, {.u32 = 128} }
    };
    
    FilterContract_t contract = {
        .input_constraints = constraints,
        .n_input_constraints = 1,
        .output_behaviors = NULL,
        .n_output_behaviors = 0
    };
    
    char error_msg[256];
    
    // Should fail - 64 < 128
    Bp_EC err = prop_validate_connection(&table, &contract, error_msg, sizeof(error_msg));
    TEST_ASSERT_EQUAL(Bp_EC_PROPERTY_MISMATCH, err);
    
    // Should pass when value meets requirement
    prop_set_min_batch_capacity(&table, 256);
    err = prop_validate_connection(&table, &contract, error_msg, sizeof(error_msg));
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
}

void test_property_propagation_set(void) {
    PropertyTable_t upstream = prop_table_init();
    prop_set_sample_rate_hz(&upstream, 48000);
    
    // Filter sets output to 44100 Hz (convert to period)
    uint64_t period_44100 = sample_rate_to_period_ns(44100);
    OutputBehavior_t behaviors[] = {
        { PROP_SAMPLE_PERIOD_NS, BEHAVIOR_OP_SET, {.u64 = period_44100} }
    };
    
    FilterContract_t contract = {
        .input_constraints = NULL,
        .n_input_constraints = 0,
        .output_behaviors = behaviors,
        .n_output_behaviors = 1
    };
    
    PropertyTable_t downstream = prop_propagate(&upstream, &contract);
    
    uint32_t rate;
    TEST_ASSERT_TRUE(prop_get_sample_rate_hz(&downstream, &rate));
    TEST_ASSERT_EQUAL(44101, rate);  // 1e9/22675 = 44100.77 rounds to 44101
}

void test_property_propagation_preserve(void) {
    PropertyTable_t upstream = prop_table_init();
    prop_set_dtype(&upstream, DTYPE_FLOAT);
    prop_set_sample_rate_hz(&upstream, 48000);
    
    // Filter preserves all properties (empty behaviors)
    FilterContract_t contract = {
        .input_constraints = NULL,
        .n_input_constraints = 0,
        .output_behaviors = NULL,
        .n_output_behaviors = 0
    };
    
    PropertyTable_t downstream = prop_propagate(&upstream, &contract);
    
    // All properties should be preserved
    SampleDtype_t dtype;
    uint32_t rate;
    TEST_ASSERT_TRUE(prop_get_dtype(&downstream, &dtype));
    TEST_ASSERT_TRUE(prop_get_sample_rate_hz(&downstream, &rate));
    TEST_ASSERT_EQUAL(DTYPE_FLOAT, dtype);
    TEST_ASSERT_EQUAL(48000, rate);
}

void test_buffer_config_properties(void) {
    // Test with full batches only (supports_partial_batches = false)
    BatchBuffer_config config = {
        .dtype = DTYPE_I32,
        .batch_capacity_expo = 6,  // 64 samples
        .ring_capacity_expo = 8,
        .overflow_behaviour = OVERFLOW_BLOCK
    };
    
    PropertyTable_t table = prop_from_buffer_config(&config);
    
    SampleDtype_t dtype;
    uint32_t min_cap, max_cap;
    
    TEST_ASSERT_TRUE(prop_get_dtype(&table, &dtype));
    TEST_ASSERT_EQUAL(DTYPE_I32, dtype);
    
    TEST_ASSERT_TRUE(prop_get_min_batch_capacity(&table, &min_cap));
    TEST_ASSERT_TRUE(prop_get_max_batch_capacity(&table, &max_cap));
    TEST_ASSERT_EQUAL(64, min_cap);
    TEST_ASSERT_EQUAL(64, max_cap);  // Both min and max are 64 for full batches
    
    // Sample rate is not in buffer config
    uint32_t rate;
    TEST_ASSERT_FALSE(prop_get_sample_rate_hz(&table, &rate));
}

void test_buffer_config_properties_partial(void) {
    // Test that filters can override min/max after init
    BatchBuffer_config config = {
        .dtype = DTYPE_FLOAT,
        .batch_capacity_expo = 7,  // 128 samples
        .ring_capacity_expo = 8,
        .overflow_behaviour = OVERFLOW_BLOCK
    };
    
    PropertyTable_t table = prop_from_buffer_config(&config);
    
    // Default should be exact capacity
    uint32_t min_cap, max_cap;
    TEST_ASSERT_TRUE(prop_get_min_batch_capacity(&table, &min_cap));
    TEST_ASSERT_TRUE(prop_get_max_batch_capacity(&table, &max_cap));
    TEST_ASSERT_EQUAL(128, min_cap);
    TEST_ASSERT_EQUAL(128, max_cap);
    
    // Filters that support partial batches would override like this:
    prop_set_min_batch_capacity(&table, 1);
    TEST_ASSERT_TRUE(prop_get_min_batch_capacity(&table, &min_cap));
    TEST_ASSERT_EQUAL(1, min_cap);    // Now accepts partial
    TEST_ASSERT_EQUAL(128, max_cap);  // Max unchanged
}

void test_signal_generator_properties(void) {
    SignalGenerator_t sg;
    SignalGenerator_config_t config = {
        .name = "test_sg",
        .buff_config = {
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 7,  // 128 samples
            .ring_capacity_expo = 8,
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .timeout_us = 1000000,
        .waveform_type = WAVEFORM_SINE,
        .frequency_hz = 1000.0,
        .phase_rad = 0.0,
        .sample_period_ns = 20833,  // ~48kHz
        .amplitude = 1.0,
        .offset = 0.0,
        .max_samples = 0,
        .allow_aliasing = false,
        .start_time_ns = 0
    };
    
    CHECK_ERR(signal_generator_init(&sg, config));
    
    // Check that signal generator sets its output properties
    SampleDtype_t dtype;
    uint32_t rate, batch_cap;
    
    TEST_ASSERT_TRUE(prop_get_dtype(&sg.base.output_properties, &dtype));
    TEST_ASSERT_EQUAL(DTYPE_FLOAT, dtype);
    
    TEST_ASSERT_TRUE(prop_get_sample_rate_hz(&sg.base.output_properties, &rate));
    TEST_ASSERT_EQUAL(48000, rate);  // 1e9 / 20833 ≈ 48000
    
    TEST_ASSERT_TRUE(prop_get_max_batch_capacity(&sg.base.output_properties, &batch_cap));
    TEST_ASSERT_EQUAL(128, batch_cap);
    
    // Signal generator is a source filter with no input constraints
    // but it should have set output properties
    TEST_ASSERT(sg.base.output_properties.properties[PROP_DATA_TYPE].known);
    TEST_ASSERT(sg.base.output_properties.properties[PROP_SAMPLE_PERIOD_NS].known);
}

void test_csv_sink_type_constraint(void) {
    // CSV sink should require float data type
    SignalGenerator_t sg;
    CSVSink_t sink;
    
    // Set up signal generator with float output
    SignalGenerator_config_t sg_config = {
        .name = "test_sg",
        .buff_config = {
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 6,
            .ring_capacity_expo = 8,
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .timeout_us = 1000000,
        .waveform_type = WAVEFORM_SINE,
        .frequency_hz = 1000.0,
        .phase_rad = 0.0,
        .sample_period_ns = 20833,  // ~48kHz
        .amplitude = 1.0,
        .offset = 0.0
    };
    
    CHECK_ERR(signal_generator_init(&sg, sg_config));
    
    // Set up CSV sink
    CSVSink_config_t sink_config = {
        .name = "test_sink",
        .buff_config = {
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 6,
            .ring_capacity_expo = 8,
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .output_path = "/tmp/test.csv",
        .append = false,
        .file_mode = 0644,
        .max_file_size_bytes = 0,
        .format = CSV_FORMAT_SIMPLE,
        .delimiter = ",",
        .line_ending = "\n",
        .write_header = true,
        .precision = 6
    };
    
    CHECK_ERR(csv_sink_init(&sink, sink_config));
    
    // Connection should succeed - types match
    Bp_EC err = filt_connect(&sg.base, 0, &sink.base, 0);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
}

void test_property_name_lookup(void) {
    TEST_ASSERT_EQUAL_STRING("data_type", prop_get_name(PROP_DATA_TYPE));
    TEST_ASSERT_EQUAL_STRING("min_batch_capacity", prop_get_name(PROP_MIN_BATCH_CAPACITY));
    TEST_ASSERT_EQUAL_STRING("max_batch_capacity", prop_get_name(PROP_MAX_BATCH_CAPACITY));
    TEST_ASSERT_EQUAL_STRING("sample_period_ns", prop_get_name(PROP_SAMPLE_PERIOD_NS));
    TEST_ASSERT_EQUAL_STRING("unknown", prop_get_name(PROP_COUNT_MVP));
}

int main(void) {
    UNITY_BEGIN();
    
    // Basic property operations
    RUN_TEST(test_property_table_init);
    RUN_TEST(test_property_setters_getters);
    
    // Constraint validation
    RUN_TEST(test_constraint_validation_exists);
    RUN_TEST(test_constraint_validation_equality);
    RUN_TEST(test_constraint_validation_range);
    
    // Property propagation
    RUN_TEST(test_property_propagation_set);
    RUN_TEST(test_property_propagation_preserve);
    
    // Integration with existing code
    RUN_TEST(test_buffer_config_properties);
    RUN_TEST(test_buffer_config_properties_partial);
    RUN_TEST(test_signal_generator_properties);
    RUN_TEST(test_csv_sink_type_constraint);
    
    // Utility functions
    RUN_TEST(test_property_name_lookup);
    
    return UNITY_END();
}