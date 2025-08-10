#include <stdio.h>
#include <string.h>
#include "bperr.h"
#include "csv_sink.h"
#include "map.h"
#include "properties.h"
#include "signal_generator.h"
#include "unity.h"
#include "utils.h"

// CHECK_ERR macro for error handling
#define CHECK_ERR(ERR)                                          \
  do {                                                          \
    Bp_EC _ec = ERR;                                            \
    TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, _ec, err_lut[_ec]); \
  } while (false);

void setUp(void) {}
void tearDown(void) {}

void test_property_table_init(void)
{
  PropertyTable_t table = prop_table_init();

  // All properties should be unknown initially
  for (int i = 0; i < PROP_COUNT_MVP; i++) {
    TEST_ASSERT_FALSE(table.properties[i].known);
  }
}

void test_property_setters_getters(void)
{
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

void test_constraint_validation_exists(void)
{
  PropertyTable_t table = prop_table_init();

  // Constraint requires sample rate to exist
  InputConstraint_t constraints[] = {
      {PROP_SAMPLE_PERIOD_NS, CONSTRAINT_OP_EXISTS, INPUT_ALL, {0}}};

  FilterContract_t contract = {.input_constraints = constraints,
                               .n_input_constraints = 1,
                               .output_behaviors = NULL,
                               .n_output_behaviors = 0};

  char error_msg[256];

  // Should fail when property is unknown
  Bp_EC err = prop_validate_connection(&table, &contract, 0, error_msg,
                                       sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_PROPERTY_MISMATCH, err);

  // Should pass when property is set
  prop_set_sample_rate_hz(&table, 48000);
  err = prop_validate_connection(&table, &contract, 0, error_msg,
                                 sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
}

void test_constraint_validation_equality(void)
{
  PropertyTable_t table = prop_table_init();
  prop_set_dtype(&table, DTYPE_FLOAT);

  // Constraint requires DTYPE_I32
  InputConstraint_t constraints[] = {
      {PROP_DATA_TYPE, CONSTRAINT_OP_EQ, INPUT_ALL, {.dtype = DTYPE_I32}}};

  FilterContract_t contract = {.input_constraints = constraints,
                               .n_input_constraints = 1,
                               .output_behaviors = NULL,
                               .n_output_behaviors = 0};

  char error_msg[256];

  // Should fail - type mismatch
  Bp_EC err = prop_validate_connection(&table, &contract, 0, error_msg,
                                       sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_PROPERTY_MISMATCH, err);

  // Should pass when types match
  prop_set_dtype(&table, DTYPE_I32);
  err = prop_validate_connection(&table, &contract, 0, error_msg,
                                 sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
}

void test_constraint_validation_range(void)
{
  PropertyTable_t table = prop_table_init();
  prop_set_min_batch_capacity(&table, 64);

  // Constraint requires min batch capacity >= 128
  InputConstraint_t constraints[] = {
      {PROP_MIN_BATCH_CAPACITY, CONSTRAINT_OP_GTE, INPUT_ALL, {.u32 = 128}}};

  FilterContract_t contract = {.input_constraints = constraints,
                               .n_input_constraints = 1,
                               .output_behaviors = NULL,
                               .n_output_behaviors = 0};

  char error_msg[256];

  // Should fail - 64 < 128
  Bp_EC err = prop_validate_connection(&table, &contract, 0, error_msg,
                                       sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_PROPERTY_MISMATCH, err);

  // Should pass when value meets requirement
  prop_set_min_batch_capacity(&table, 256);
  err = prop_validate_connection(&table, &contract, 0, error_msg,
                                 sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
}

void test_property_propagation_set(void)
{
  PropertyTable_t upstream = prop_table_init();
  prop_set_sample_rate_hz(&upstream, 48000);

  // Filter sets output to 44100 Hz (convert to period)
  uint64_t period_44100 = sample_rate_to_period_ns(44100);
  OutputBehavior_t behaviors[] = {{PROP_SAMPLE_PERIOD_NS,
                                   BEHAVIOR_OP_SET,
                                   OUTPUT_ALL,
                                   {.u64 = period_44100}}};

  FilterContract_t contract = {.input_constraints = NULL,
                               .n_input_constraints = 0,
                               .output_behaviors = behaviors,
                               .n_output_behaviors = 1};

  PropertyTable_t downstream = prop_propagate(&upstream, &contract);

  uint32_t rate;
  TEST_ASSERT_TRUE(prop_get_sample_rate_hz(&downstream, &rate));
  TEST_ASSERT_EQUAL(44101, rate);  // 1e9/22675 = 44100.77 rounds to 44101
}

void test_property_propagation_preserve(void)
{
  PropertyTable_t upstream = prop_table_init();
  prop_set_dtype(&upstream, DTYPE_FLOAT);
  prop_set_sample_rate_hz(&upstream, 48000);

  // Filter preserves all properties (empty behaviors)
  FilterContract_t contract = {.input_constraints = NULL,
                               .n_input_constraints = 0,
                               .output_behaviors = NULL,
                               .n_output_behaviors = 0};

  PropertyTable_t downstream = prop_propagate(&upstream, &contract);

  // All properties should be preserved
  SampleDtype_t dtype;
  uint32_t rate;
  TEST_ASSERT_TRUE(prop_get_dtype(&downstream, &dtype));
  TEST_ASSERT_TRUE(prop_get_sample_rate_hz(&downstream, &rate));
  TEST_ASSERT_EQUAL(DTYPE_FLOAT, dtype);
  TEST_ASSERT_EQUAL(48000, rate);
}

void test_buffer_config_properties(void)
{
  // Test with full batches only (supports_partial_batches = false)
  BatchBuffer_config config = {.dtype = DTYPE_I32,
                               .batch_capacity_expo = 6,  // 64 samples
                               .ring_capacity_expo = 8,
                               .overflow_behaviour = OVERFLOW_BLOCK};

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

void test_buffer_config_properties_partial(void)
{
  // Test that filters can override min/max after init
  BatchBuffer_config config = {.dtype = DTYPE_FLOAT,
                               .batch_capacity_expo = 7,  // 128 samples
                               .ring_capacity_expo = 8,
                               .overflow_behaviour = OVERFLOW_BLOCK};

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

void test_signal_generator_properties(void)
{
  SignalGenerator_t sg;
  SignalGenerator_config_t config = {
      .name = "test_sg",
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 7,  // 128 samples
                      .ring_capacity_expo = 8,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000000,
      .waveform_type = WAVEFORM_SINE,
      .frequency_hz = 1000.0,
      .phase_rad = 0.0,
      .sample_period_ns = 20833,  // ~48kHz
      .amplitude = 1.0,
      .offset = 0.0,
      .max_samples = 0,
      .allow_aliasing = false,
      .start_time_ns = 0};

  CHECK_ERR(signal_generator_init(&sg, config));

  // Check that signal generator sets its output properties
  SampleDtype_t dtype;
  uint32_t rate, batch_cap;

  TEST_ASSERT_TRUE(prop_get_dtype(&sg.base.output_properties, &dtype));
  TEST_ASSERT_EQUAL(DTYPE_FLOAT, dtype);

  TEST_ASSERT_TRUE(prop_get_sample_rate_hz(&sg.base.output_properties, &rate));
  TEST_ASSERT_EQUAL(48000, rate);  // 1e9 / 20833 ≈ 48000

  TEST_ASSERT_TRUE(
      prop_get_max_batch_capacity(&sg.base.output_properties, &batch_cap));
  TEST_ASSERT_EQUAL(128, batch_cap);

  // Signal generator is a source filter with no input constraints
  // but it should have set output properties
  TEST_ASSERT(sg.base.output_properties.properties[PROP_DATA_TYPE].known);
  TEST_ASSERT(
      sg.base.output_properties.properties[PROP_SAMPLE_PERIOD_NS].known);
}

void test_csv_sink_type_constraint(void)
{
  // CSV sink should require float data type
  SignalGenerator_t sg;
  CSVSink_t sink;

  // Set up signal generator with float output
  SignalGenerator_config_t sg_config = {
      .name = "test_sg",
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 8,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000000,
      .waveform_type = WAVEFORM_SINE,
      .frequency_hz = 1000.0,
      .phase_rad = 0.0,
      .sample_period_ns = 20833,  // ~48kHz
      .amplitude = 1.0,
      .offset = 0.0};

  CHECK_ERR(signal_generator_init(&sg, sg_config));

  // Set up CSV sink
  CSVSink_config_t sink_config = {
      .name = "test_sink",
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 8,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .output_path = "/tmp/test.csv",
      .append = false,
      .file_mode = 0644,
      .max_file_size_bytes = 0,
      .format = CSV_FORMAT_SIMPLE,
      .delimiter = ",",
      .line_ending = "\n",
      .write_header = true,
      .precision = 6};

  CHECK_ERR(csv_sink_init(&sink, sink_config));

  // Connection should succeed - types match
  Bp_EC err = filt_connect(&sg.base, 0, &sink.base, 0);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
}

void test_property_name_lookup(void)
{
  TEST_ASSERT_EQUAL_STRING("data_type", prop_get_name(PROP_DATA_TYPE));
  TEST_ASSERT_EQUAL_STRING("min_batch_capacity",
                           prop_get_name(PROP_MIN_BATCH_CAPACITY));
  TEST_ASSERT_EQUAL_STRING("max_batch_capacity",
                           prop_get_name(PROP_MAX_BATCH_CAPACITY));
  TEST_ASSERT_EQUAL_STRING("sample_period_ns",
                           prop_get_name(PROP_SAMPLE_PERIOD_NS));
  TEST_ASSERT_EQUAL_STRING("unknown", prop_get_name(PROP_COUNT_MVP));
}

// Port-specific constraint and behavior tests
void test_port_specific_constraint_validation(void)
{
  PropertyTable_t table = prop_table_init();
  prop_set_dtype(&table, DTYPE_FLOAT);

  // Create constraints for different ports
  InputConstraint_t constraints[2] = {
      {PROP_DATA_TYPE, CONSTRAINT_OP_EQ, INPUT_0, {.dtype = DTYPE_FLOAT}},
      {PROP_DATA_TYPE, CONSTRAINT_OP_EQ, INPUT_1, {.dtype = DTYPE_I32}}};

  FilterContract_t contract = {.input_constraints = constraints,
                               .n_input_constraints = 2,
                               .output_behaviors = NULL,
                               .n_output_behaviors = 0};

  char error_msg[256];

  // Input port 0 should pass (DTYPE_FLOAT matches)
  Bp_EC err = prop_validate_connection(&table, &contract, 0, error_msg,
                                       sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // Input port 1 should fail (DTYPE_FLOAT != DTYPE_I32)
  err = prop_validate_connection(&table, &contract, 1, error_msg,
                                 sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_PROPERTY_MISMATCH, err);

  // Input port 2 should pass (no constraints apply to port 2)
  err = prop_validate_connection(&table, &contract, 2, error_msg,
                                 sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
}

void test_input_all_mask_validation(void)
{
  PropertyTable_t table = prop_table_init();
  prop_set_dtype(&table, DTYPE_FLOAT);

  // Constraint applies to all input ports
  InputConstraint_t constraints[1] = {
      {PROP_DATA_TYPE, CONSTRAINT_OP_EQ, INPUT_ALL, {.dtype = DTYPE_FLOAT}}};

  FilterContract_t contract = {.input_constraints = constraints,
                               .n_input_constraints = 1,
                               .output_behaviors = NULL,
                               .n_output_behaviors = 0};

  char error_msg[256];

  // All ports should be validated against INPUT_ALL constraint
  for (int port = 0; port < 4; port++) {
    Bp_EC err = prop_validate_connection(&table, &contract, port, error_msg,
                                         sizeof(error_msg));
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  }
}

void test_bitmask_combination_constraints(void)
{
  PropertyTable_t table = prop_table_init();
  prop_set_dtype(&table, DTYPE_FLOAT);

  // Constraint applies to INPUT_0 | INPUT_2 (ports 0 and 2)
  InputConstraint_t constraints[1] = {{PROP_DATA_TYPE,
                                       CONSTRAINT_OP_EQ,
                                       INPUT_0 | INPUT_2,
                                       {.dtype = DTYPE_FLOAT}}};

  FilterContract_t contract = {.input_constraints = constraints,
                               .n_input_constraints = 1,
                               .output_behaviors = NULL,
                               .n_output_behaviors = 0};

  char error_msg[256];

  // Port 0 should be validated (included in mask)
  Bp_EC err = prop_validate_connection(&table, &contract, 0, error_msg,
                                       sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // Port 1 should pass (not included in mask, so constraint doesn't apply)
  err = prop_validate_connection(&table, &contract, 1, error_msg,
                                 sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // Port 2 should be validated (included in mask)
  err = prop_validate_connection(&table, &contract, 2, error_msg,
                                 sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // Port 3 should pass (not included in mask)
  err = prop_validate_connection(&table, &contract, 3, error_msg,
                                 sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
}

void test_port_mask_constants(void)
{
  // Test that the port mask constants have expected values
  TEST_ASSERT_EQUAL_UINT32(0x00000001, INPUT_0);
  TEST_ASSERT_EQUAL_UINT32(0x00000002, INPUT_1);
  TEST_ASSERT_EQUAL_UINT32(0x00000004, INPUT_2);
  TEST_ASSERT_EQUAL_UINT32(0x00000008, INPUT_3);
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFF, INPUT_ALL);

  TEST_ASSERT_EQUAL_UINT32(0x00000001, OUTPUT_0);
  TEST_ASSERT_EQUAL_UINT32(0x00000002, OUTPUT_1);
  TEST_ASSERT_EQUAL_UINT32(0x00000004, OUTPUT_2);
  TEST_ASSERT_EQUAL_UINT32(0x00000008, OUTPUT_3);
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFF, OUTPUT_ALL);

  // Test combinations
  TEST_ASSERT_EQUAL_UINT32(0x00000003, INPUT_0 | INPUT_1);
  TEST_ASSERT_EQUAL_UINT32(0x0000000C, INPUT_2 | INPUT_3);
}

/* Multi-input alignment constraint tests */

void test_multi_input_alignment_matching_properties(void)
{
  // Create a mock filter with multi-input alignment constraint
  Filter_t filter;
  Core_filt_config_t config = {
      .name = "test_alignment_filter",
      .filt_type = FILT_T_MAP,
      .size = sizeof(Filter_t),
      .n_inputs = 2,
      .max_supported_sinks = 1,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 8,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000000,
      .worker = &matched_passthroug};

  CHECK_ERR(filt_init(&filter, config));

  // Add multi-input alignment constraint for data type on inputs 0 and 1
  TEST_ASSERT_TRUE(prop_append_constraint(&filter, PROP_DATA_TYPE,
                                          CONSTRAINT_OP_MULTI_INPUT_ALIGNED,
                                          NULL, INPUT_0 | INPUT_1));

  // Create source properties with matching data types
  PropertyTable_t source1_props = prop_table_init();
  PropertyTable_t source2_props = prop_table_init();
  prop_set_dtype(&source1_props, DTYPE_FLOAT);
  prop_set_dtype(&source2_props, DTYPE_FLOAT);

  char error_msg[256];

  // First connection should pass (no other inputs to compare against yet)
  Bp_EC err = prop_validate_multi_input_alignment(&filter, 0, &source1_props,
                                                  error_msg, sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // Simulate first connection being stored
  filter.input_properties[0] = source1_props;

  // Second connection should pass (matching data type)
  err = prop_validate_multi_input_alignment(&filter, 1, &source2_props,
                                            error_msg, sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  CHECK_ERR(filt_deinit(&filter));
}

void test_multi_input_alignment_mismatched_properties(void)
{
  // Create a mock filter with multi-input alignment constraint
  Filter_t filter;
  Core_filt_config_t config = {
      .name = "test_alignment_filter",
      .filt_type = FILT_T_MAP,
      .size = sizeof(Filter_t),
      .n_inputs = 2,
      .max_supported_sinks = 1,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 8,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000000,
      .worker = &matched_passthroug};

  CHECK_ERR(filt_init(&filter, config));

  // Add multi-input alignment constraint for data type on inputs 0 and 1
  TEST_ASSERT_TRUE(prop_append_constraint(&filter, PROP_DATA_TYPE,
                                          CONSTRAINT_OP_MULTI_INPUT_ALIGNED,
                                          NULL, INPUT_0 | INPUT_1));

  // Create source properties with mismatched data types
  PropertyTable_t source1_props = prop_table_init();
  PropertyTable_t source2_props = prop_table_init();
  prop_set_dtype(&source1_props, DTYPE_FLOAT);
  prop_set_dtype(&source2_props, DTYPE_I32);

  char error_msg[256];

  // First connection should pass
  Bp_EC err = prop_validate_multi_input_alignment(&filter, 0, &source1_props,
                                                  error_msg, sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // Simulate first connection being stored
  filter.input_properties[0] = source1_props;

  // Second connection should fail (mismatched data type)
  err = prop_validate_multi_input_alignment(&filter, 1, &source2_props,
                                            error_msg, sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_PROPERTY_MISMATCH, err);

  // Check that error message is meaningful
  TEST_ASSERT_TRUE(strlen(error_msg) > 0);
  TEST_ASSERT_TRUE(strstr(error_msg, "data type mismatch") != NULL);

  CHECK_ERR(filt_deinit(&filter));
}

void test_multi_input_alignment_input_all_mask(void)
{
  // Create a mock filter with multi-input alignment constraint
  Filter_t filter;
  Core_filt_config_t config = {
      .name = "test_alignment_filter",
      .filt_type = FILT_T_MAP,
      .size = sizeof(Filter_t),
      .n_inputs = 3,
      .max_supported_sinks = 1,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 8,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000000,
      .worker = &matched_passthroug};

  CHECK_ERR(filt_init(&filter, config));

  // Add multi-input alignment constraint for sample period on all inputs
  TEST_ASSERT_TRUE(prop_append_constraint(&filter, PROP_SAMPLE_PERIOD_NS,
                                          CONSTRAINT_OP_MULTI_INPUT_ALIGNED,
                                          NULL, INPUT_ALL));

  // Create source properties with matching sample periods
  PropertyTable_t source1_props = prop_table_init();
  PropertyTable_t source2_props = prop_table_init();
  PropertyTable_t source3_props = prop_table_init();
  prop_set_sample_period(&source1_props, 1000000);  // 1ms period
  prop_set_sample_period(&source2_props, 1000000);  // 1ms period
  prop_set_sample_period(&source3_props, 1000000);  // 1ms period

  char error_msg[256];

  // First connection should pass
  Bp_EC err = prop_validate_multi_input_alignment(&filter, 0, &source1_props,
                                                  error_msg, sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  filter.input_properties[0] = source1_props;

  // Second connection should pass
  err = prop_validate_multi_input_alignment(&filter, 1, &source2_props,
                                            error_msg, sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  filter.input_properties[1] = source2_props;

  // Third connection should pass (all have matching periods)
  err = prop_validate_multi_input_alignment(&filter, 2, &source3_props,
                                            error_msg, sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  CHECK_ERR(filt_deinit(&filter));
}

void test_multi_input_alignment_specific_input_masks(void)
{
  // Create a mock filter with multi-input alignment constraint
  Filter_t filter;
  Core_filt_config_t config = {
      .name = "test_alignment_filter",
      .filt_type = FILT_T_MAP,
      .size = sizeof(Filter_t),
      .n_inputs = 4,
      .max_supported_sinks = 1,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 8,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000000,
      .worker = &matched_passthroug};

  CHECK_ERR(filt_init(&filter, config));

  // Add multi-input alignment constraint for data type on inputs 0 and 1 only
  TEST_ASSERT_TRUE(prop_append_constraint(&filter, PROP_DATA_TYPE,
                                          CONSTRAINT_OP_MULTI_INPUT_ALIGNED,
                                          NULL, INPUT_0 | INPUT_1));

  // Add separate alignment constraint for inputs 2 and 3
  TEST_ASSERT_TRUE(prop_append_constraint(&filter, PROP_DATA_TYPE,
                                          CONSTRAINT_OP_MULTI_INPUT_ALIGNED,
                                          NULL, INPUT_2 | INPUT_3));

  // Create source properties - inputs 0,1 have FLOAT, inputs 2,3 have I32
  PropertyTable_t source_props[4];
  for (int i = 0; i < 4; i++) {
    source_props[i] = prop_table_init();
  }
  prop_set_dtype(&source_props[0], DTYPE_FLOAT);
  prop_set_dtype(&source_props[1], DTYPE_FLOAT);
  prop_set_dtype(&source_props[2], DTYPE_I32);
  prop_set_dtype(&source_props[3], DTYPE_I32);

  char error_msg[256];

  // Connect inputs 0 and 1 - should both pass
  Bp_EC err = prop_validate_multi_input_alignment(&filter, 0, &source_props[0],
                                                  error_msg, sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  filter.input_properties[0] = source_props[0];

  err = prop_validate_multi_input_alignment(&filter, 1, &source_props[1],
                                            error_msg, sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  filter.input_properties[1] = source_props[1];

  // Connect inputs 2 and 3 - should both pass
  err = prop_validate_multi_input_alignment(&filter, 2, &source_props[2],
                                            error_msg, sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  filter.input_properties[2] = source_props[2];

  err = prop_validate_multi_input_alignment(&filter, 3, &source_props[3],
                                            error_msg, sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  CHECK_ERR(filt_deinit(&filter));
}

void test_multi_input_alignment_unknown_properties(void)
{
  // Create a mock filter with multi-input alignment constraint
  Filter_t filter;
  Core_filt_config_t config = {
      .name = "test_alignment_filter",
      .filt_type = FILT_T_MAP,
      .size = sizeof(Filter_t),
      .n_inputs = 2,
      .max_supported_sinks = 1,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 8,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000000,
      .worker = &matched_passthroug};

  CHECK_ERR(filt_init(&filter, config));

  // Add multi-input alignment constraint for data type
  TEST_ASSERT_TRUE(prop_append_constraint(&filter, PROP_DATA_TYPE,
                                          CONSTRAINT_OP_MULTI_INPUT_ALIGNED,
                                          NULL, INPUT_0 | INPUT_1));

  // Create source properties - first has known type, second has unknown type
  PropertyTable_t source1_props = prop_table_init();
  PropertyTable_t source2_props = prop_table_init();
  prop_set_dtype(&source1_props, DTYPE_FLOAT);
  // source2_props deliberately left with unknown data type

  char error_msg[256];

  // First connection should pass
  Bp_EC err = prop_validate_multi_input_alignment(&filter, 0, &source1_props,
                                                  error_msg, sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  filter.input_properties[0] = source1_props;

  // Second connection should fail (unknown property)
  err = prop_validate_multi_input_alignment(&filter, 1, &source2_props,
                                            error_msg, sizeof(error_msg));
  TEST_ASSERT_EQUAL(Bp_EC_PROPERTY_MISMATCH, err);

  // Check error message mentions property is not known
  TEST_ASSERT_TRUE(strlen(error_msg) > 0);
  TEST_ASSERT_TRUE(strstr(error_msg, "not known") != NULL);

  CHECK_ERR(filt_deinit(&filter));
}

int main(void)
{
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

  // Port-specific constraint and behavior tests
  RUN_TEST(test_port_specific_constraint_validation);
  RUN_TEST(test_input_all_mask_validation);
  RUN_TEST(test_bitmask_combination_constraints);
  RUN_TEST(test_port_mask_constants);

  // Multi-input alignment constraint tests
  RUN_TEST(test_multi_input_alignment_matching_properties);
  RUN_TEST(test_multi_input_alignment_mismatched_properties);
  RUN_TEST(test_multi_input_alignment_input_all_mask);
  RUN_TEST(test_multi_input_alignment_specific_input_masks);
  RUN_TEST(test_multi_input_alignment_unknown_properties);

  return UNITY_END();
}