/**
 * @file test_connection_single_sink.c
 * @brief Test single sink connection
 */

#include "common.h"

void test_connection_single_sink(void)
{
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  SKIP_IF_NO_OUTPUTS();

  // Create consumer
  ControllableConsumer_t consumer = {0};  // Zero-initialize
  ControllableConsumerConfig_t consumer_config = {
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

  err = controllable_consumer_init(&consumer, consumer_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // Connect
  err = filt_sink_connect(g_fut, 0, consumer.base.input_buffers[0]);
  TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Connection failed");
  TEST_ASSERT_NOT_NULL_MESSAGE(g_fut->sinks[0],
                               "Sink not set after connection");
  TEST_ASSERT_EQUAL_PTR_MESSAGE(consumer.base.input_buffers[0], g_fut->sinks[0],
                                "Sink pointer mismatch");

  // Disconnect
  err = filt_sink_disconnect(g_fut, 0);
  TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, err, "Disconnection failed");
  TEST_ASSERT_NULL_MESSAGE(g_fut->sinks[0],
                           "Sink not cleared after disconnect");

  // Cleanup
  filt_deinit(&consumer.base);
  filt_deinit(g_fut);
}