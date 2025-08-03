/**
 * @file test_connection_multi_sink.c
 * @brief Test multiple sink connections
 */

#include "common.h"

void test_connection_multi_sink(void)
{
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  SKIP_IF_NO_OUTPUTS();

  // Get the filter's maximum supported sinks
  size_t max_sinks = g_fut->max_supported_sinks;

  // Skip if filter doesn't support multiple outputs
  if (max_sinks < 2) {
    TEST_IGNORE_MESSAGE("Filter doesn't support multiple outputs");
    return;
  }

  // Limit test to reasonable number of sinks
  size_t test_sinks = (max_sinks > MAX_SINKS) ? MAX_SINKS : max_sinks;
  if (test_sinks > 8) test_sinks = 8;  // Reasonable limit for testing

  // Create consumers up to the maximum supported
  ControllableConsumer_t* consumers =
      calloc(test_sinks + 1, sizeof(ControllableConsumer_t));
  TEST_ASSERT_NOT_NULL_MESSAGE(consumers, "Failed to allocate consumers");

  // Test connecting up to the maximum
  for (size_t i = 0; i < test_sinks; i++) {
    ControllableConsumerConfig_t config = {
        .name = "test_consumer",
        .buff_config = {.dtype = (g_fut->n_input_buffers > 0 &&
                                  g_fut->input_buffers[0])
                                     ? g_fut->input_buffers[0]->dtype
                                     : DTYPE_FLOAT,
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

    err = controllable_consumer_init(&consumers[i], config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);

    // Connect - should succeed for indices < max_supported_sinks
    err = filt_sink_connect(g_fut, i, consumers[i].base.input_buffers[0]);
    TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err,
                              "Failed to connect within max_supported_sinks");
    TEST_ASSERT_NOT_NULL(g_fut->sinks[i]);
  }

  // Verify all connections
  for (size_t i = 0; i < test_sinks; i++) {
    TEST_ASSERT_EQUAL_PTR(consumers[i].base.input_buffers[0], g_fut->sinks[i]);
  }

  // Test connecting one more than the maximum (should fail)
  if (test_sinks < max_sinks) {
    // Create one more consumer
    ControllableConsumerConfig_t config = {
        .name = "test_consumer_extra",
        .buff_config = {.dtype = (g_fut->n_input_buffers > 0 &&
                                  g_fut->input_buffers[0])
                                     ? g_fut->input_buffers[0]->dtype
                                     : DTYPE_FLOAT,
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

    err = controllable_consumer_init(&consumers[test_sinks], config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);

    // This connection should fail
    err = filt_sink_connect(g_fut, max_sinks,
                            consumers[test_sinks].base.input_buffers[0]);
    TEST_ASSERT_EQUAL_MESSAGE(
        Bp_EC_INVALID_SINK_IDX, err,
        "Should fail when connecting beyond max_supported_sinks");

    // Cleanup the extra consumer
    filt_deinit(&consumers[test_sinks].base);
  }

  // Cleanup
  for (size_t i = 0; i < test_sinks; i++) {
    filt_deinit(&consumers[i].base);
  }
  free(consumers);
  filt_deinit(g_fut);
}