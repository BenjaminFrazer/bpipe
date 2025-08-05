/**
 * @file test_connection_type_safety.c
 * @brief Test type safety in connections
 */

#include "common.h"

void test_connection_type_safety(void)
{
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  SKIP_IF_NO_OUTPUTS();
  SKIP_IF_NO_INPUTS();

  // For filters with inputs, check type safety
  if (g_fut->n_input_buffers == 0) {
    TEST_IGNORE_MESSAGE("Filter has no inputs to test type safety");
    return;
  }

  // Get the expected data type from the filter's input buffer
  SampleDtype_t expected_dtype = g_fut->input_buffers[0]->dtype;

  // Create consumer with wrong data type
  SampleDtype_t wrong_dtype =
      (expected_dtype == DTYPE_FLOAT) ? DTYPE_I32 : DTYPE_FLOAT;

  ControllableConsumer_t consumer = {0};  // Zero-initialize
  ControllableConsumerConfig_t consumer_config = {
      .name = "test_consumer",
      .buff_config = {.dtype = wrong_dtype,  // Intentionally wrong type
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 8,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000000,
      .process_delay_us = 0,
      .validate_sequence = false,
      .validate_timing = false,
      .consume_pattern = 0,
      .slow_start = false,
      .slow_start_batches = 0};

  err = controllable_consumer_init(&consumer, consumer_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // Connection should fail due to type mismatch
  err = filt_sink_connect(g_fut, 0, consumer.base.input_buffers[0]);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(Bp_EC_OK, err,
                                "Connection should fail with type mismatch");

  // Cleanup
  filt_deinit(&consumer.base);
  filt_deinit(g_fut);
}