#include "batch_buffer.h"
#include "bperr.h"
#include "unity_internals.h"
#define _DEFAULT_SOURCE  // For usleep
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include "../bpipe/tee.h"
#include "../lib/Unity/src/unity.h"
#include "time.h"

struct timespec ts_1ms = {.tv_nsec = 1000000};      //
struct timespec ts_10ms = {.tv_nsec = 10000000};    //
struct timespec ts_100ms = {.tv_nsec = 100000000};  //

#define CHECK_ERR(ERR)                                          \
  do {                                                          \
    Bp_EC _ec = ERR;                                            \
    TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, _ec, err_lut[_ec]); \
  } while (false);

// Helper function to verify continuous sequence
static void verify_sequence(Batch_buff_t* buffer, uint32_t start_value,
                            size_t expected_samples)
{
  uint32_t current = start_value;
  size_t total_samples = 0;
  Bp_EC err;

  while (total_samples < expected_samples) {
    Batch_t* batch = bb_get_tail(buffer, 10000, &err);  // Non-blocking check
    // bb_print(buffer);
    CHECK_ERR(err);
    if (!batch) {
      printf("  No batch available, total_samples=%zu, expected=%zu\n",
             total_samples, expected_samples);
      break;
    }

    float* data = (float*) batch->data;
    for (size_t i = 0; i < batch->head; i++) {
      TEST_ASSERT_EQUAL_FLOAT((float) current, data[i]);
      current++;
      total_samples++;
    }

    bb_del_tail(buffer);
  }

  TEST_ASSERT_EQUAL(expected_samples, total_samples);
  // With 0-based counting, if we have 320 samples starting from 0,
  // the final sample value should be 319
  if (expected_samples > 0 && start_value == 0) {
    TEST_ASSERT_EQUAL(expected_samples - 1, current - 1);
  }
}

// Helper function to fill buffer with sequential data
static void fill_sequential_data(Batch_buff_t* buffer, uint32_t* counter,
                                 size_t n_batches)
{
  for (size_t b = 0; b < n_batches; b++) {
    Batch_t* batch = bb_get_head(buffer);
    TEST_ASSERT_NOT_NULL(batch);

    float* data = (float*) batch->data;
    size_t batch_size = bb_batch_size(buffer);
    for (size_t i = 0; i < batch_size; i++) {
      data[i] = (float) (*counter)++;
    }
    batch->head = batch_size;
    batch->t_ns = 1000000 * b;
    batch->period_ns = 1000;

    CHECK_ERR(bb_submit(buffer, 1000));
  }
}

void setUp(void)
{
  // Test setup
}

void tearDown(void)
{
  // Test cleanup
}

// Test 1: Basic dual output functionality
void test_tee_dual_output(void)
{
  printf("\n=== Testing Basic Dual Output ===\n");
  fflush(stdout);

  // Configure two identical outputs
  BatchBuffer_config out_configs[2] = {{.dtype = DTYPE_FLOAT,
                                        .batch_capacity_expo = 6,  // 64 samples
                                        .ring_capacity_expo = 4,   // 15 batches
                                        .overflow_behaviour = OVERFLOW_BLOCK},
                                       {.dtype = DTYPE_FLOAT,
                                        .batch_capacity_expo = 6,  // 64 samples
                                        .ring_capacity_expo = 4,   // 15 batches
                                        .overflow_behaviour = OVERFLOW_BLOCK}};

  Tee_config_t config = {.name = "test_dual_tee",
                         .buff_config = out_configs[0],  // Input buffer config
                         .n_outputs = 2,
                         .output_configs = out_configs,
                         .timeout_us = 1000,
                         .copy_data = true};

  // Initialize tee
  Tee_filt_t tee;
  printf("Initializing tee...\n");
  fflush(stdout);
  CHECK_ERR(tee_init(&tee, config));
  printf("Tee initialized\n");
  fflush(stdout);

  // Create output buffers
  Batch_buff_t output1, output2;
  printf("Creating output buffers...\n");
  fflush(stdout);
  CHECK_ERR(bb_init(&output1, "output1", out_configs[0]));
  CHECK_ERR(bb_init(&output2, "output2", out_configs[1]));
  printf("Output buffers created\n");
  fflush(stdout);

  // Connect outputs
  printf("Connecting outputs...\n");
  fflush(stdout);
  CHECK_ERR(filt_sink_connect(&tee.base, 0, &output1));
  CHECK_ERR(filt_sink_connect(&tee.base, 1, &output2));
  printf("Outputs connected\n");
  fflush(stdout);

  // Start processing
  printf("Starting filters...\n");
  fflush(stdout);
  CHECK_ERR(filt_start(&tee.base));
  printf("Tee started\n");
  fflush(stdout);
  CHECK_ERR(bb_start(&output1));
  printf("Output1 started\n");
  fflush(stdout);
  CHECK_ERR(bb_start(&output2));
  printf("Output2 started\n");
  fflush(stdout);

  // Need to start the input buffer too!
  printf("Starting input buffer...\n");
  fflush(stdout);
  CHECK_ERR(bb_start(&tee.base.input_buffers[0]));
  printf("Input buffer started\n");
  fflush(stdout);

  // Submit test data
  uint32_t counter = 0;
  printf("Submitting test data...\n");
  fflush(stdout);
  fill_sequential_data(&tee.base.input_buffers[0], &counter, 5);
  printf("Test data submitted\n");
  fflush(stdout);

  // Wait for processing
  printf("Waiting for processing...\n");
  fflush(stdout);
  nanosleep(&ts_100ms, NULL);
  printf("Wait complete\n");
  fflush(stdout);

  // Verify both outputs received identical data
  printf("Verifying output1...\n");
  fflush(stdout);
  verify_sequence(&output1, 0, 320);  // 5 batches * 64 samples
  printf("Output1 verified\n");
  fflush(stdout);
  printf("Verifying output2...\n");
  fflush(stdout);
  verify_sequence(&output2, 0, 320);
  printf("Output2 verified\n");
  fflush(stdout);

  // Check metrics - we submitted 5 batches
  TEST_ASSERT_EQUAL(5, tee.successful_writes[0]);
  TEST_ASSERT_EQUAL(5, tee.successful_writes[1]);

  // Cleanup
  CHECK_ERR(filt_stop(&tee.base));
  CHECK_ERR(bb_stop(&output1));
  CHECK_ERR(bb_stop(&output2));
  CHECK_ERR(filt_deinit(&tee.base));
  CHECK_ERR(bb_deinit(&output1));
  CHECK_ERR(bb_deinit(&output2));

  printf("Dual output test passed!\n");
}

// Test 2: Maximum outputs
void test_tee_max_outputs(void)
{
  printf("\n=== Testing Maximum Outputs ===\n");

  // Configure MAX_SINKS outputs
  BatchBuffer_config out_configs[MAX_SINKS];
  for (size_t i = 0; i < MAX_SINKS; i++) {
    out_configs[i] =
        (BatchBuffer_config){.dtype = DTYPE_FLOAT,
                             .batch_capacity_expo = 5,  // 32 samples
                             .ring_capacity_expo = 3,   // 7 batches
                             .overflow_behaviour = OVERFLOW_BLOCK};
  }

  Tee_config_t config = {.name = "test_max_tee",
                         .buff_config = out_configs[0],  // Input buffer config
                         .n_outputs = MAX_SINKS,
                         .output_configs = out_configs,
                         .timeout_us = 1000,
                         .copy_data = true};

  // Initialize tee
  Tee_filt_t tee;
  CHECK_ERR(tee_init(&tee, config));

  // Create and connect output buffers
  Batch_buff_t outputs[MAX_SINKS];
  for (size_t i = 0; i < MAX_SINKS; i++) {
    char name[32];
    snprintf(name, sizeof(name), "output%zu", i);
    CHECK_ERR(bb_init(&outputs[i], name, out_configs[i]));
    CHECK_ERR(filt_sink_connect(&tee.base, i, &outputs[i]));
    CHECK_ERR(bb_start(&outputs[i]));
  }

  // Start processing
  CHECK_ERR(filt_start(&tee.base));

  // Submit test data
  uint32_t counter = 0;
  fill_sequential_data(&tee.base.input_buffers[0], &counter, 3);

  // Wait for processing
  nanosleep(&ts_10ms, NULL);

  // Verify all outputs received data
  for (size_t i = 0; i < MAX_SINKS; i++) {
    verify_sequence(&outputs[i], 0, 96);  // 3 batches * 32 samples
  }

  // Cleanup
  CHECK_ERR(filt_stop(&tee.base));
  for (size_t i = 0; i < MAX_SINKS; i++) {
    CHECK_ERR(bb_stop(&outputs[i]));
    CHECK_ERR(bb_deinit(&outputs[i]));
  }
  CHECK_ERR(filt_deinit(&tee.base));

  printf("Max outputs test passed!\n");
}

// Test 3: Type mismatch validation
void test_tee_type_mismatch(void)
{
  printf("\n=== Testing Type Mismatch Validation ===\n");

  BatchBuffer_config out_configs[2] = {{.dtype = DTYPE_FLOAT,
                                        .batch_capacity_expo = 6,
                                        .ring_capacity_expo = 4,
                                        .overflow_behaviour = OVERFLOW_BLOCK},
                                       {.dtype = DTYPE_I32,  // Different type!
                                        .batch_capacity_expo = 6,
                                        .ring_capacity_expo = 4,
                                        .overflow_behaviour = OVERFLOW_BLOCK}};

  Tee_config_t config = {.name = "test_mismatch",
                         .buff_config = out_configs[0],  // Input buffer config
                         .n_outputs = 2,
                         .output_configs = out_configs,
                         .timeout_us = 1000,
                         .copy_data = true};

  Tee_filt_t tee;
  Bp_EC err = tee_init(&tee, config);
  TEST_ASSERT_EQUAL(Bp_EC_TYPE_MISMATCH, err);

  printf("Type mismatch validation passed!\n");
}

// Test 4: Mixed overflow behavior
void test_tee_mixed_overflow_behavior(void)
{
  printf("\n=== Testing Mixed Overflow Behavior ===\n");

  BatchBuffer_config out_configs[2] = {
      {.dtype = DTYPE_FLOAT,
       .batch_capacity_expo = 6,  // 64 samples
       .ring_capacity_expo = 3,   // 7 batches (small)
       .overflow_behaviour = OVERFLOW_BLOCK},
      {.dtype = DTYPE_FLOAT,
       .batch_capacity_expo = 6,  // 64 samples
       .ring_capacity_expo = 3,   // 7 batches (small)
       .overflow_behaviour = OVERFLOW_DROP_TAIL}};

  Tee_config_t config = {.name = "test_mixed_overflow",
                         .buff_config = out_configs[0],  // Input buffer config
                         .n_outputs = 2,
                         .output_configs = out_configs,
                         .timeout_us = 100,  // Short timeout
                         .copy_data = true};

  // Initialize tee
  Tee_filt_t tee;
  CHECK_ERR(tee_init(&tee, config));

  // Create output buffers
  Batch_buff_t output1, output2;
  CHECK_ERR(bb_init(&output1, "blocking_out", out_configs[0]));
  CHECK_ERR(bb_init(&output2, "dropping_out", out_configs[1]));

  // Connect outputs
  CHECK_ERR(filt_sink_connect(&tee.base, 0, &output1));
  CHECK_ERR(filt_sink_connect(&tee.base, 1, &output2));

  // Start processing
  CHECK_ERR(filt_start(&tee.base));
  CHECK_ERR(bb_start(&output1));
  CHECK_ERR(bb_start(&output2));

  // Submit more data than buffers can hold
  uint32_t counter = 0;
  fill_sequential_data(&tee.base.input_buffers[0], &counter,
                       10);  // Overflow condition

  // Let some processing happen
  nanosleep(&ts_10ms, NULL);

  // Consume from blocking output to unblock
  for (int i = 0; i < 5; i++) {
    Bp_EC err;
    Batch_t* batch = bb_get_tail(&output1, 100, &err);
    if (batch) {
      bb_del_tail(&output1);
    }
  }

  // Wait for more processing
  nanosleep(&ts_10ms, NULL);

  // Both outputs should have received data (dropping may have lost some)
  TEST_ASSERT_GREATER_THAN(0, tee.successful_writes[0]);
  TEST_ASSERT_GREATER_THAN(0, tee.successful_writes[1]);

  // Priority output (0) should have received all its data
  printf("Output 0 (blocking) writes: %zu\n", tee.successful_writes[0]);
  printf("Output 1 (dropping) writes: %zu\n", tee.successful_writes[1]);

  // Cleanup
  CHECK_ERR(filt_stop(&tee.base));
  CHECK_ERR(bb_stop(&output1));
  CHECK_ERR(bb_stop(&output2));
  CHECK_ERR(filt_deinit(&tee.base));
  CHECK_ERR(bb_deinit(&output1));
  CHECK_ERR(bb_deinit(&output2));

  printf("Mixed overflow behavior test passed!\n");
}

// Test 5: Priority output latency
void test_tee_priority_output_latency(void)
{
  printf("\n=== Testing Priority Output Latency ===\n");

  BatchBuffer_config out_configs[3] = {
      {.dtype = DTYPE_FLOAT,
       .batch_capacity_expo = 6,
       .ring_capacity_expo = 5,  // Large buffer
       .overflow_behaviour = OVERFLOW_BLOCK},
      {.dtype = DTYPE_FLOAT,
       .batch_capacity_expo = 6,
       .ring_capacity_expo = 2,  // Very small buffer
       .overflow_behaviour = OVERFLOW_DROP_TAIL},
      {.dtype = DTYPE_FLOAT,
       .batch_capacity_expo = 6,
       .ring_capacity_expo = 2,  // Very small buffer
       .overflow_behaviour = OVERFLOW_DROP_TAIL}};

  Tee_config_t config = {.name = "test_priority",
                         .buff_config = out_configs[0],  // Input buffer config
                         .n_outputs = 3,
                         .output_configs = out_configs,
                         .timeout_us = 1000,
                         .copy_data = true};

  // Initialize tee
  Tee_filt_t tee;
  CHECK_ERR(tee_init(&tee, config));

  // Create output buffers
  Batch_buff_t outputs[3];
  CHECK_ERR(bb_init(&outputs[0], "priority_out", out_configs[0]));
  CHECK_ERR(bb_init(&outputs[1], "slow_out1", out_configs[1]));
  CHECK_ERR(bb_init(&outputs[2], "slow_out2", out_configs[2]));

  // Connect outputs
  for (size_t i = 0; i < 3; i++) {
    CHECK_ERR(filt_sink_connect(&tee.base, i, &outputs[i]));
    CHECK_ERR(bb_start(&outputs[i]));
  }

  // Start processing
  CHECK_ERR(filt_start(&tee.base));

  // Submit data
  uint32_t counter = 0;
  fill_sequential_data(&tee.base.input_buffers[0], &counter, 20);

  // Wait briefly
  nanosleep(&ts_10ms, NULL);

  // Priority output should have received most/all data
  // Secondary outputs may have dropped some
  printf("Priority output (0) writes: %zu\n", tee.successful_writes[0]);
  printf("Secondary output (1) writes: %zu\n", tee.successful_writes[1]);
  printf("Secondary output (2) writes: %zu\n", tee.successful_writes[2]);

  TEST_ASSERT_GREATER_OR_EQUAL(
      15, tee.successful_writes[0]);  // Should get most data

  // Cleanup
  CHECK_ERR(filt_stop(&tee.base));
  for (size_t i = 0; i < 3; i++) {
    CHECK_ERR(bb_stop(&outputs[i]));
    CHECK_ERR(bb_deinit(&outputs[i]));
  }
  CHECK_ERR(filt_deinit(&tee.base));

  printf("Priority output latency test passed!\n");
}

// Test 6: Graceful shutdown with pending data
void test_tee_graceful_shutdown(void)
{
  printf("\n=== Testing Graceful Shutdown ===\n");

  BatchBuffer_config out_configs[2] = {{.dtype = DTYPE_FLOAT,
                                        .batch_capacity_expo = 6,
                                        .ring_capacity_expo = 5,
                                        .overflow_behaviour = OVERFLOW_BLOCK},
                                       {.dtype = DTYPE_FLOAT,
                                        .batch_capacity_expo = 6,
                                        .ring_capacity_expo = 5,
                                        .overflow_behaviour = OVERFLOW_BLOCK}};

  Tee_config_t config = {.name = "test_shutdown",
                         .buff_config = out_configs[0],  // Input buffer config
                         .n_outputs = 2,
                         .output_configs = out_configs,
                         .timeout_us = 1000,
                         .copy_data = true};

  // Initialize
  Tee_filt_t tee;
  CHECK_ERR(tee_init(&tee, config));

  Batch_buff_t output1, output2;
  CHECK_ERR(bb_init(&output1, "out1", out_configs[0]));
  CHECK_ERR(bb_init(&output2, "out2", out_configs[1]));

  CHECK_ERR(filt_sink_connect(&tee.base, 0, &output1));
  CHECK_ERR(filt_sink_connect(&tee.base, 1, &output2));

  CHECK_ERR(filt_start(&tee.base));
  CHECK_ERR(bb_start(&output1));
  CHECK_ERR(bb_start(&output2));

  // Submit data but don't consume it
  uint32_t counter = 0;
  fill_sequential_data(&tee.base.input_buffers[0], &counter, 10);

  // Brief wait
  nanosleep(&ts_10ms, NULL);

  // Stop tee - should wait for outputs to flush
  CHECK_ERR(filt_stop(&tee.base));

  // Now consume the data
  size_t consumed1 = 0, consumed2 = 0;
  Bp_EC err;

  while (true) {
    Batch_t* batch = bb_get_tail(&output1, 100, &err);
    if (!batch) break;
    consumed1 += batch->head;
    bb_del_tail(&output1);
  }

  while (true) {
    Batch_t* batch = bb_get_tail(&output2, 100, &err);
    if (!batch) break;
    consumed2 += batch->head;
    bb_del_tail(&output2);
  }

  // Should have received all data
  TEST_ASSERT_EQUAL(640, consumed1);  // 10 * 64
  TEST_ASSERT_EQUAL(640, consumed2);

  // Cleanup
  CHECK_ERR(bb_stop(&output1));
  CHECK_ERR(bb_stop(&output2));
  CHECK_ERR(filt_deinit(&tee.base));
  CHECK_ERR(bb_deinit(&output1));
  CHECK_ERR(bb_deinit(&output2));

  printf("Graceful shutdown test passed!\n");
}

// Test 7: Invalid configuration
void test_tee_invalid_config(void)
{
  printf("\n=== Testing Invalid Configuration ===\n");

  BatchBuffer_config out_configs[2] = {
      {.dtype = DTYPE_FLOAT, .batch_capacity_expo = 6, .ring_capacity_expo = 4},
      {.dtype = DTYPE_FLOAT,
       .batch_capacity_expo = 6,
       .ring_capacity_expo = 4}};

  Tee_filt_t tee;
  Bp_EC err;

  // Test: Too few outputs
  Tee_config_t config1 = {.name = "test",
                          .buff_config = out_configs[0],
                          .n_outputs = 1,  // Invalid: minimum is 2
                          .output_configs = out_configs,
                          .timeout_us = 1000,
                          .copy_data = true};
  err = tee_init(&tee, config1);
  TEST_ASSERT_EQUAL(Bp_EC_INVALID_CONFIG, err);

  // Test: Too many outputs
  Tee_config_t config2 = {
      .name = "test",
      .buff_config = out_configs[0],
      .n_outputs = MAX_SINKS + 1,  // Invalid: exceeds maximum
      .output_configs = out_configs,
      .timeout_us = 1000,
      .copy_data = true};
  err = tee_init(&tee, config2);
  TEST_ASSERT_EQUAL(Bp_EC_INVALID_CONFIG, err);

  // Test: Null output configs
  Tee_config_t config3 = {.name = "test",
                          .buff_config = out_configs[0],
                          .n_outputs = 2,
                          .output_configs = NULL,  // Invalid: NULL pointer
                          .timeout_us = 1000,
                          .copy_data = true};
  err = tee_init(&tee, config3);
  TEST_ASSERT_EQUAL(Bp_EC_NULL_POINTER, err);

  printf("Invalid configuration test passed!\n");
}

// Test 8: Batch size validation
void test_tee_batch_size_validation(void)
{
  printf("\n=== Testing Batch Size Validation ===\n");

  Tee_filt_t tee;
  Bp_EC err;

  // Test 1: Mismatched input/output batch sizes
  printf("Test 1: Output batch size different from input...\n");
  BatchBuffer_config mismatched_configs[2] = {
      {.dtype = DTYPE_FLOAT,
       .batch_capacity_expo = 6,  // 64 samples - different from input!
       .ring_capacity_expo = 4,
       .overflow_behaviour = OVERFLOW_BLOCK},
      {.dtype = DTYPE_FLOAT,
       .batch_capacity_expo = 6,  // 64 samples
       .ring_capacity_expo = 4,
       .overflow_behaviour = OVERFLOW_BLOCK}};

  BatchBuffer_config input_config = {.dtype = DTYPE_FLOAT,
                                     .batch_capacity_expo = 7,  // 128 samples
                                     .ring_capacity_expo = 4,
                                     .overflow_behaviour = OVERFLOW_BLOCK};

  Tee_config_t config1 = {.name = "mismatched_batch_tee",
                          .buff_config = input_config,
                          .n_outputs = 2,
                          .output_configs = mismatched_configs,
                          .timeout_us = 1000,
                          .copy_data = true};

  err = tee_init(&tee, config1);
  TEST_ASSERT_EQUAL(Bp_EC_INVALID_CONFIG, err);
  printf("  ✓ Correctly rejected mismatched batch sizes\n");

  // Test 2: All matching batch sizes (should succeed)
  printf("Test 2: All batch sizes matching...\n");
  BatchBuffer_config matched_configs[3] = {
      {.dtype = DTYPE_FLOAT,
       .batch_capacity_expo = 7,  // 128 samples - matches input
       .ring_capacity_expo = 4,
       .overflow_behaviour = OVERFLOW_BLOCK},
      {.dtype = DTYPE_FLOAT,
       .batch_capacity_expo = 7,  // 128 samples - matches input
       .ring_capacity_expo = 5,   // Different ring size is OK
       .overflow_behaviour = OVERFLOW_DROP_TAIL},  // Different overflow is OK
      {.dtype = DTYPE_FLOAT,
       .batch_capacity_expo = 7,  // 128 samples - matches input
       .ring_capacity_expo = 3,
       .overflow_behaviour = OVERFLOW_BLOCK}};

  Tee_config_t config2 = {.name = "matched_batch_tee",
                          .buff_config = input_config,
                          .n_outputs = 3,
                          .output_configs = matched_configs,
                          .timeout_us = 1000,
                          .copy_data = true};

  err = tee_init(&tee, config2);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  printf("  ✓ Successfully initialized with matching batch sizes\n");

  // Clean up the successful initialization
  filt_deinit(&tee.base);

  // Test 3: One output with different batch size
  printf("Test 3: One output with different batch size...\n");
  BatchBuffer_config mixed_configs[3] = {
      {.dtype = DTYPE_FLOAT,
       .batch_capacity_expo = 7,  // 128 samples - matches
       .ring_capacity_expo = 4,
       .overflow_behaviour = OVERFLOW_BLOCK},
      {.dtype = DTYPE_FLOAT,
       .batch_capacity_expo = 7,  // 128 samples - matches
       .ring_capacity_expo = 4,
       .overflow_behaviour = OVERFLOW_BLOCK},
      {.dtype = DTYPE_FLOAT,
       .batch_capacity_expo = 6,  // 64 samples - MISMATCH!
       .ring_capacity_expo = 4,
       .overflow_behaviour = OVERFLOW_BLOCK}};

  Tee_config_t config3 = {.name = "mixed_batch_tee",
                          .buff_config = input_config,
                          .n_outputs = 3,
                          .output_configs = mixed_configs,
                          .timeout_us = 1000,
                          .copy_data = true};

  err = tee_init(&tee, config3);
  TEST_ASSERT_EQUAL(Bp_EC_INVALID_CONFIG, err);
  printf("  ✓ Correctly rejected when one output has different batch size\n");

  printf("Batch size validation test passed!\n");
}

// Test 9: Pipeline integration
void test_tee_pipeline_integration(void)
{
  printf("\n=== Testing Pipeline Integration ===\n");

  // This test would require map filter to be available
  // For now, just test basic connectivity

  BatchBuffer_config out_configs[2] = {{.dtype = DTYPE_FLOAT,
                                        .batch_capacity_expo = 6,
                                        .ring_capacity_expo = 4,
                                        .overflow_behaviour = OVERFLOW_BLOCK},
                                       {.dtype = DTYPE_FLOAT,
                                        .batch_capacity_expo = 6,
                                        .ring_capacity_expo = 4,
                                        .overflow_behaviour = OVERFLOW_BLOCK}};

  Tee_config_t config = {.name = "pipeline_tee",
                         .buff_config = out_configs[0],  // Input buffer config
                         .n_outputs = 2,
                         .output_configs = out_configs,
                         .timeout_us = 1000,
                         .copy_data = true};

  Tee_filt_t tee;
  CHECK_ERR(tee_init(&tee, config));

  // Create downstream filters (using passthrough for now)
  Filter_t downstream1, downstream2;
  Core_filt_config_t downstream_config = {
      .name = "downstream",
      .filt_type = FILT_T_MATCHED_PASSTHROUGH,
      .size = sizeof(Filter_t),
      .n_inputs = 1,
      .max_supported_sinks = 1,
      .buff_config = out_configs[0],
      .timeout_us = 1000,
      .worker = matched_passthroug};

  CHECK_ERR(filt_init(&downstream1, downstream_config));
  downstream_config.name = "downstream2";
  CHECK_ERR(filt_init(&downstream2, downstream_config));

  // Connect: tee → [downstream1, downstream2]
  CHECK_ERR(filt_sink_connect(&tee.base, 0, &downstream1.input_buffers[0]));
  CHECK_ERR(filt_sink_connect(&tee.base, 1, &downstream2.input_buffers[0]));

  // Create final outputs
  Batch_buff_t final1, final2;
  CHECK_ERR(bb_init(&final1, "final1", out_configs[0]));
  CHECK_ERR(bb_init(&final2, "final2", out_configs[1]));

  CHECK_ERR(filt_sink_connect(&downstream1, 0, &final1));
  CHECK_ERR(filt_sink_connect(&downstream2, 0, &final2));

  // Start pipeline - start filters first
  CHECK_ERR(filt_start(&tee.base));
  CHECK_ERR(filt_start(&downstream1));
  CHECK_ERR(filt_start(&downstream2));

  // Give worker threads time to start
  nanosleep(&ts_10ms, NULL);

  // Then start buffers
  CHECK_ERR(bb_start(&tee.base.input_buffers[0]));  // Start input buffer
  CHECK_ERR(bb_start(
      &downstream1.input_buffers[0]));  // Start downstream input buffers
  CHECK_ERR(bb_start(&downstream2.input_buffers[0]));
  CHECK_ERR(bb_start(&final1));
  CHECK_ERR(bb_start(&final2));

  // Give buffer startup time to propagate through pipeline
  nanosleep(&ts_10ms, NULL);

  // Submit data
  uint32_t counter = 0;
  TEST_MESSAGE("Filling input");
  fill_sequential_data(&tee.base.input_buffers[0], &counter, 5);

  // Wait for all threads to start up properly
  nanosleep(&ts_10ms, NULL);

  // Wait for processing with more time for complex pipeline
  nanosleep(&ts_100ms, NULL);
  CHECK_ERR(tee.base.worker_err_info.ec);
  CHECK_ERR(downstream1.worker_err_info.ec);
  CHECK_ERR(downstream2.worker_err_info.ec);

  TEST_ASSERT_EQUAL_INT_MESSAGE(5, tee.base.metrics.n_batches,
                                "Tee should be passed 5 batches");

  TEST_ASSERT_EQUAL_INT_MESSAGE(5, downstream1.metrics.n_batches,
                                "should be passed 5 batches");

  TEST_ASSERT_EQUAL_INT_MESSAGE(5, downstream2.metrics.n_batches,
                                "should be passed 5 batches");

  TEST_ASSERT_EQUAL_INT_MESSAGE(5, bb_occupancy(&final1),
                                "should be passed 5 batches");

  TEST_ASSERT_EQUAL_INT_MESSAGE(5, bb_occupancy(&final2),
                                "should be passed 5 batches");
  // Verify data reached final outputs
  //
  TEST_MESSAGE("Verifying final1");
  verify_sequence(&final1, 0, 320);
  TEST_MESSAGE("Verifying final2");
  verify_sequence(&final2, 0, 320);

  // Cleanup
  TEST_MESSAGE("Stopping filters");
  CHECK_ERR(filt_stop(&tee.base));
  CHECK_ERR(filt_stop(&downstream1));
  CHECK_ERR(filt_stop(&downstream2));
  TEST_MESSAGE("Stopping final batches");
  CHECK_ERR(bb_stop(&final1));
  CHECK_ERR(bb_stop(&final2));
  TEST_MESSAGE("De-init");
  CHECK_ERR(filt_deinit(&tee.base));
  CHECK_ERR(filt_deinit(&downstream1));
  CHECK_ERR(filt_deinit(&downstream2));
  CHECK_ERR(bb_deinit(&final1));
  CHECK_ERR(bb_deinit(&final2));

  printf("Pipeline integration test passed!\n");
}

int main(void)
{
  UNITY_BEGIN();

  RUN_TEST(test_tee_dual_output);
  RUN_TEST(test_tee_max_outputs);
  RUN_TEST(test_tee_type_mismatch);
  RUN_TEST(test_tee_mixed_overflow_behavior);
  RUN_TEST(test_tee_priority_output_latency);
  RUN_TEST(test_tee_graceful_shutdown);
  RUN_TEST(test_tee_invalid_config);
  RUN_TEST(test_tee_batch_size_validation);
  RUN_TEST(test_tee_pipeline_integration);

  return UNITY_END();
}
